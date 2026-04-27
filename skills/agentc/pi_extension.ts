import { ExtensionContext } from "@mariozechner/pi-coding-agent";
import { AgentCSubstrate } from "./agentc";
import * as path from 'path';

/**
 * Pi Extension: AgentC Integration
 * Registers the 'agentc_eval' tool which interfaces with the persistent
 * Edict VM backend via the AgentCSubstrate skill.
 */
export function activate(ctx: ExtensionContext) {
    // Configure paths
    const pipeDir = process.env.AGENTC_PIPE_DIR || '/tmp';
    const inputPipe = path.join(pipeDir, 'agentc_in.pipe');
    const outputPipe = path.join(pipeDir, 'agentc_out.pipe');
    const edictPath = process.env.EDICT_PATH || path.join(__dirname, '../../build/edict/edict');

    // Initialize the Substrate
    const substrate = new AgentCSubstrate(inputPipe, outputPipe, edictPath);

    // Register tool
    ctx.api.registerTool({
        name: "agentc_eval",
        description: "Evaluate an Edict program in the AgentC cognitive substrate.",
        parameters: { 
            type: "object", 
            properties: { 
                edict_code: { type: "string", description: "The Edict code to evaluate" },
                speculative: { type: "boolean", description: "Whether to wrap execution in a transaction" }
            }, 
            required: ["edict_code"] 
        },
        execute: async (args) => {
            try {
                if (args.speculative) {
                    return await substrate.speculate(args.edict_code);
                } else {
                    return await substrate.eval(args.edict_code);
                }
            } catch (err: any) {
                return `Error: ${err.message}`;
            }
        }
    });

    // Clean up on deactivation
    ctx.subscriptions.push({
        dispose: async () => {
            await substrate.dispose();
        }
    });
}
