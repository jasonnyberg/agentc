import { AgentCSubstrate } from './agentc';
import * as path from 'path';

async function runDemo() {
    const edictPath = path.join(__dirname, '../../build/edict/edict');
    const socketPath = '/tmp/agentc_demo.sock';
    
    console.log("--- 1. Launching VM in Socket Mode ---");
    const substrate = await AgentCSubstrate.createSocket(socketPath, edictPath);
    
    console.log("--- 2. Interacting with VM ---");
    const result = await substrate.eval("'Socket-Verified-Live' ! stack");
    console.log("Result received:", result);
    
    console.log("--- 3. Exiting VM ---");
    await substrate.dispose();
    console.log("VM Shutdown cleanly.");
}

runDemo().catch(console.error);
