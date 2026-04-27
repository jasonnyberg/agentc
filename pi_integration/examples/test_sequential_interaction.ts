import { AgentCSubstrate } from './agentc';
import * as path from 'path';

/**
 * Sequential Stress Test
 * Evaluates 10 sequential stack operations to ensure non-blocking,
 * persistent socket communication remains stable under load.
 */
async function runSequentialDemo() {
    const edictPath = path.join(__dirname, '../../build/edict/edict');
    const socketPath = '/tmp/sequential_test.sock';
    
    console.log("--- Launching VM for Sequential Test ---");
    const substrate = await AgentCSubstrate.createSocket(socketPath, edictPath);
    
    try {
        for (let i = 1; i <= 10; i++) {
            console.log(`--- Operation ${i} ---`);
            const command = `['item_${i}'] stack !`;
            const result = await substrate.eval(command);
            console.log(`Result ${i}:`, result);
        }
    } catch (e) {
        console.error("Sequence interrupted:", e);
    } finally {
        await substrate.dispose();
        console.log("--- Sequential Test Cleanup ---");
    }
}

runSequentialDemo().catch(console.error);
