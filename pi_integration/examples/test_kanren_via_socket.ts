import { AgentCSubstrate } from './agentc';
import * as path from 'path';

/**
 * Verification Script: Kanren Logic via Unix Domain Sockets
 * 
 * Verifies that the AgentCSubstrate can successfully transport
 * logic queries to the Edict VM and receive results.
 */
async function verifyKanrenSocket() {
    const edictPath = path.join(__dirname, '../../build/edict/edict');
    const socketPath = '/tmp/kanren_verify.sock';

    console.log("--- Initializing Logic Socket Substrate ---");
    const substrate = await AgentCSubstrate.createSocket(socketPath, edictPath);

    try {
        console.log("--- Importing Logic FFI ---");
        const libPath = path.resolve(__dirname, '../../build/kanren/libkanren.so');
        const hdrPath = path.resolve(__dirname, '../../cartographer/tests/kanren_runtime_ffi_poc.h');
        
        await substrate.importFFI(libPath, hdrPath, "logicffi", { "agentc_logic_eval_ltv": "logic" });

        console.log("--- Sending Kanren Query ---");
        // Canonical Spec as defined in K028
        const query = {
            "fresh": ["q"],
            "where": [["==", "q", "tea"]],
            "results": ["q"]
        };
        
        // This relies on the VM having the logic capability bound correctly
        const result = await substrate.queryLogic(query);
        console.log("Logic Result:", result);

    } catch (e) {
        console.error("Logic Query Failed:", e);
    } finally {
        await substrate.dispose();
        console.log("--- Cleanup Complete ---");
    }
}

verifyKanrenSocket().catch(console.error);
