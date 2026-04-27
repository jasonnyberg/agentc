import { ExtensionContext } from "@mariozechner/pi-coding-agent";
import { AgentCSubstrate } from "./agentc";
import * as path from 'path';

/**
 * Pi Extension: AgentC Integration
 * Registers the 'agentc_eval' tool which interfaces with the persistent
 * Edict VM backend via the AgentCSubstrate skill.
 */
export async function activate(ctx: ExtensionContext) {
    // Configure paths
    const edictPath = process.env.EDICT_PATH || path.join(__dirname, '../../build/edict/edict');
    const socketPath = process.env.AGENTC_SOCKET_PATH || '/tmp/agentc.sock';
    const useSocket = process.env.AGENTC_USE_SOCKET === 'true';

    // Initialize the Substrate
    let substrate: AgentCSubstrate;
    if (useSocket) {
        substrate = await AgentCSubstrate.createSocket(socketPath, edictPath);
    } else {
        const pipeDir = process.env.AGENTC_PIPE_DIR || '/tmp';
        substrate = await AgentCSubstrate.createPipe(
            path.join(pipeDir, 'agentc_in.pipe'),
            path.join(pipeDir, 'agentc_out.pipe'),
            edictPath
        );
    }

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
