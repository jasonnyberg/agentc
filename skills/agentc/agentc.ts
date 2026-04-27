import { ChildProcess, spawn } from 'child_process';
import { EventEmitter } from 'events';
import * as fs from 'fs';
import * as readline from 'readline';
import * as path from 'path';

export class AgentCSubstrate extends EventEmitter {
    private input: fs.WriteStream;
    private output: fs.ReadStream;
    private rl: readline.Interface;
    private pendingResolve: ((value: string) => void) | null = null;
    private process: ChildProcess;

    constructor(inputPipe: string, outputPipe: string, edictPath: string) {
        super();

        // Spawn Edict VM process
        this.process = spawn(edictPath, ['--ipc', inputPipe, outputPipe]);

        // Small wait or check for pipe existence
        this.input = fs.createWriteStream(inputPipe);
        this.output = fs.createReadStream(outputPipe);
        
        this.rl = readline.createInterface({
            input: this.output,
            terminal: false
        });

        this.rl.on('line', (line) => {
            if (line.startsWith('=> ') && this.pendingResolve) {
                this.pendingResolve(line.substring(3));
                this.pendingResolve = null;
            } else if (line.startsWith('Error: ') && this.pendingResolve) {
                this.pendingResolve = null;
                throw new Error(line);
            }
        });
    }

    async eval(code: string): Promise<string> {
        return new Promise((resolve, reject) => {
            this.pendingResolve = resolve;
            this.input.write(code + '\n');
        });
    }

    async speculate(code: string): Promise<string> {
        const txCode = `beginTransaction ! ${code} commit !`;
        try {
            return await this.eval(txCode);
        } catch (e) {
            await this.eval("rollback !");
            throw e;
        }
    }

    async dispose() {
        this.input.write('exit\n');
        this.process.kill();
    }
}
