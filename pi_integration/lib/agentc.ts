import { ChildProcess, spawn } from 'child_process';
import { EventEmitter } from 'events';
import * as net from 'net';
import * as fs from 'fs';
import * as readline from 'readline';
import * as stream from 'stream';

export class AgentCSubstrate extends EventEmitter {
    private transport: stream.Duplex;
    private process: ChildProcess | null = null;
    private rl: readline.Interface;
    private pendingResolve: ((value: string) => void) | null = null;

    constructor(transport: stream.Duplex, process: ChildProcess | null = null) {
        super();
        this.transport = transport;
        this.process = process;

        this.rl = readline.createInterface({
            input: this.transport,
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

    static async createSocket(socketPath: string, edictPath: string): Promise<AgentCSubstrate> {
        const proc = spawn(edictPath, ['--socket', socketPath]);
        await new Promise(r => setTimeout(r, 500));
        const client = net.connect({ path: socketPath });
        return new AgentCSubstrate(client, proc);
    }

    static async createPipe(inputPipe: string, outputPipe: string, edictPath: string): Promise<AgentCSubstrate> {
        const proc = spawn(edictPath, ['--ipc', inputPipe, outputPipe]);
        await new Promise(r => setTimeout(r, 500));
        const input = fs.createWriteStream(inputPipe);
        const output = fs.createReadStream(outputPipe);
        const duplex = stream.Duplex.from({ readable: output, writable: input });
        return new AgentCSubstrate(duplex, proc);
    }

    async eval(code: string): Promise<string> {
        return new Promise((resolve) => {
            this.pendingResolve = resolve;
            this.transport.write(code + '\n');
        });
    }

    /**
     * Imports a C/C++ shared library into the Edict VM via Cartographer FFI.
     * Automatically enables unsafe extensions and maps the specified bindings.
     */
    async importFFI(libraryPath: string, headerPath: string, moduleAlias: string, bindings: Record<string, string> = {}): Promise<string> {
        let cmd = `unsafe_extensions_allow ! pop\n`;
        // Important: paths must be pushed in brackets to avoid quote parsing issues inside Edict
        cmd += `[${libraryPath}] [${headerPath}] resolver.import ! @${moduleAlias}\n`;
        for (const [ffiName, edictName] of Object.entries(bindings)) {
            cmd += `${moduleAlias}.${ffiName} @${edictName}\n`;
        }
        return await this.eval(cmd);
    }

    /**
     * Logic Query helper using the imported capability surface.
     * Note: Expects the logic engine to be bound as 'logic' in the VM.
     */
    async queryLogic(spec: object): Promise<string> {
        // Stringify spec to a raw JSON map, which Edict natively parses as a dictionary
        const jsonSpec = JSON.stringify(spec);
        // Push the dictionary, push the logic evaluator, then execute (!)
        const command = `${jsonSpec} logic !`;
        return await this.eval(command);
    }

    async dispose() {
        this.transport.write('exit\n');
        if (this.process) this.process.kill();
    }
}
