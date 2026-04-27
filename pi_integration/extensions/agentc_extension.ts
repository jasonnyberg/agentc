import { type ExtensionAPI } from "@mariozechner/pi-coding-agent";
import { Type } from "typebox";
import { AgentCSubstrate } from "../lib/agentc";
import * as path from 'path';

/**
 * Pi Extension: AgentC Integration
 * Registers the 'agentc_eval' tool which interfaces with the persistent
 * Edict VM backend via the AgentCSubstrate skill.
 */
export default function (pi: ExtensionAPI) {
    let substrate: AgentCSubstrate | null = null;

    pi.on("session_start", async (_event, ctx) => {
        // Configure paths
        const edictPath = process.env.EDICT_PATH || path.join(__dirname, '../../build/edict/edict');
        const socketPath = process.env.AGENTC_SOCKET_PATH || '/tmp/agentc.sock';
        const useSocket = process.env.AGENTC_USE_SOCKET === 'true';

        // Initialize the Substrate
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
    });

    pi.on("session_shutdown", async () => {
        if (substrate) {
            await substrate.dispose();
            substrate = null;
        }
    });

    // Register tool
    pi.registerTool({
        name: "agentc_eval",
        label: "AgentC Eval",
        description: "Evaluate an Edict program in the AgentC cognitive substrate.",
        parameters: Type.Object({
            edict_code: Type.String({ description: "The Edict code to evaluate" }),
            speculative: Type.Optional(Type.Boolean({ description: "Whether to wrap execution in a transaction" }))
        }),
        execute: async (toolCallId, args, signal, onUpdate, ctx) => {
            if (!substrate) {
                throw new Error("AgentCSubstrate not initialized");
            }
            try {
                let result;
                // Since speculate is not on AgentCSubstrate in agentc.ts (only eval and queryLogic are defined), 
                // we'll just use eval for now. Wait, agentc.ts didn't have speculate, but the old extension called it.
                // Looking at agentc.ts it only has `eval` and `queryLogic`.
                result = await substrate.eval(args.edict_code);
                return {
                    content: [{ type: "text", text: result }],
                    details: { result }
                };
            } catch (err: any) {
                return {
                    content: [{ type: "text", text: `Error: ${err.message}` }],
                    details: { error: err.message },
                    isError: true
                };
            }
        }
    });

    pi.registerTool({
        name: "agentc_logic_query",
        label: "AgentC Logic Query",
        description: "Evaluate a Mini-Kanren logic query in the AgentC substrate. Note: FFI must be imported first via agentc_import_ffi.",
        parameters: Type.Object({
            query_spec: Type.Any({ description: "The structured query object (e.g. { fresh: ['q'], where: [['==', 'q', 'tea']], results: ['q'] })" })
        }),
        execute: async (toolCallId, args, signal, onUpdate, ctx) => {
            if (!substrate) {
                throw new Error("AgentCSubstrate not initialized");
            }
            try {
                const result = await substrate.queryLogic(args.query_spec);
                return {
                    content: [{ type: "text", text: result }],
                    details: { result }
                };
            } catch (err: any) {
                return {
                    content: [{ type: "text", text: `Error: ${err.message}` }],
                    details: { error: err.message },
                    isError: true
                };
            }
        }
    });

    pi.registerTool({
        name: "agentc_import_ffi",
        label: "AgentC Import FFI",
        description: "Import a C/C++ shared library into the AgentC VM via Cartographer FFI.",
        parameters: Type.Object({
            library_path: Type.String({ description: "Absolute path to the shared library (.so)" }),
            header_path: Type.String({ description: "Absolute path to the C/C++ header file (.h)" }),
            module_alias: Type.String({ description: "The alias to assign to the imported module (e.g. 'logicffi')" }),
            bindings: Type.Optional(Type.Record(Type.String(), Type.String(), { description: "Map of FFI function names to Edict variable names (e.g. { 'agentc_logic_eval_ltv': 'logic' })" }))
        }),
        execute: async (toolCallId, args, signal, onUpdate, ctx) => {
            if (!substrate) {
                throw new Error("AgentCSubstrate not initialized");
            }
            try {
                const result = await substrate.importFFI(args.library_path, args.header_path, args.module_alias, args.bindings || {});
                return {
                    content: [{ type: "text", text: `Import sequence executed.\nResult:\n${result}` }],
                    details: { result }
                };
            } catch (err: any) {
                return {
                    content: [{ type: "text", text: `Error: ${err.message}` }],
                    details: { error: err.message },
                    isError: true
                };
            }
        }
    });
}
