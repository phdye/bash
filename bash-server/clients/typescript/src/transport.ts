/**
 * Transport implementations for bash-server connections.
 */

import { createConnection, Socket } from "net";
import { ChildProcess, spawn } from "child_process";
import { createReadStream, createWriteStream } from "fs";
import { Readable, Writable } from "stream";
import { createInterface, Interface } from "readline";
import { TransportError } from "./errors";

/** Abstract transport interface. */
export interface Transport {
  /** Read one NDJSON line. */
  readLine(): Promise<string>;
  /** Write bytes. */
  write(data: Buffer): Promise<void>;
  /** Close the transport. */
  close(): Promise<void>;
  /** Whether the transport is open. */
  readonly isOpen: boolean;
}

/** Base class with readline-based line reading. */
abstract class BaseTransport implements Transport {
  protected _open = false;
  protected _rl: Interface | null = null;
  protected _lineQueue: string[] = [];
  protected _lineResolve: ((line: string) => void) | null = null;
  protected _errorReject: ((err: Error) => void) | null = null;

  protected setupReadline(input: Readable): void {
    this._rl = createInterface({ input, crlfDelay: Infinity });
    this._rl.on("line", (line) => {
      if (this._lineResolve) {
        const resolve = this._lineResolve;
        this._lineResolve = null;
        this._errorReject = null;
        resolve(line + "\n");
      } else {
        this._lineQueue.push(line + "\n");
      }
    });
    this._rl.on("close", () => {
      this._open = false;
      if (this._errorReject) {
        this._errorReject(new TransportError("connection closed"));
        this._lineResolve = null;
        this._errorReject = null;
      }
    });
    this._rl.on("error", (err) => {
      this._open = false;
      if (this._errorReject) {
        this._errorReject(new TransportError(`read error: ${err.message}`));
        this._lineResolve = null;
        this._errorReject = null;
      }
    });
  }

  async readLine(): Promise<string> {
    if (this._lineQueue.length > 0) {
      return this._lineQueue.shift()!;
    }
    if (!this._open) {
      throw new TransportError("not connected");
    }
    return new Promise<string>((resolve, reject) => {
      this._lineResolve = resolve;
      this._errorReject = reject;
    });
  }

  abstract write(data: Buffer): Promise<void>;
  abstract close(): Promise<void>;

  get isOpen(): boolean {
    return this._open;
  }
}

/** Unix domain socket transport. */
export class UnixSocketTransport extends BaseTransport {
  private _socket: Socket | null = null;

  async connect(path: string): Promise<void> {
    return new Promise((resolve, reject) => {
      const sock = createConnection({ path }, () => {
        this._socket = sock;
        this._open = true;
        this.setupReadline(sock);
        resolve();
      });
      sock.on("error", (err) => {
        if (!this._open) {
          reject(new TransportError(`cannot connect to ${path}: ${err.message}`));
        } else {
          this._open = false;
        }
      });
    });
  }

  async write(data: Buffer): Promise<void> {
    if (!this._socket || !this._open) {
      throw new TransportError("not connected");
    }
    return new Promise((resolve, reject) => {
      this._socket!.write(data, (err) => {
        if (err) reject(new TransportError(`write error: ${err.message}`));
        else resolve();
      });
    });
  }

  async close(): Promise<void> {
    this._open = false;
    this._rl?.close();
    this._socket?.destroy();
  }
}

/** Stdio transport (subprocess). */
export class StdioTransport extends BaseTransport {
  private _process: ChildProcess | null = null;

  async connectProcess(...args: string[]): Promise<void> {
    const [cmd, ...cmdArgs] = args;
    try {
      this._process = spawn(cmd, cmdArgs, {
        stdio: ["pipe", "pipe", "pipe"],
      });
      if (!this._process.stdout || !this._process.stdin) {
        throw new Error("failed to create stdio pipes");
      }
      this._open = true;
      this.setupReadline(this._process.stdout);
      this._process.on("exit", () => {
        this._open = false;
      });
      this._process.on("error", (err) => {
        this._open = false;
      });
    } catch (e: any) {
      throw new TransportError(`cannot start process: ${e.message}`);
    }
  }

  connectStreams(input: Readable, output: Writable): void {
    this._open = true;
    this._output = output;
    this.setupReadline(input);
  }

  private _output: Writable | null = null;

  async write(data: Buffer): Promise<void> {
    const target = this._output ?? this._process?.stdin;
    if (!target || !this._open) {
      throw new TransportError("not connected");
    }
    return new Promise((resolve, reject) => {
      target.write(data, (err) => {
        if (err) reject(new TransportError(`write error: ${err.message}`));
        else resolve();
      });
    });
  }

  async close(): Promise<void> {
    this._open = false;
    this._rl?.close();
    if (this._process) {
      this._process.kill();
      this._process = null;
    }
  }
}

/** File descriptor transport. */
export class FdTransport extends BaseTransport {
  private _writeStream: Writable | null = null;

  async connect(fd: number): Promise<void> {
    try {
      const readStream = createReadStream("", { fd, autoClose: false });
      this._writeStream = createWriteStream("", { fd, autoClose: false });
      this._open = true;
      this.setupReadline(readStream);
    } catch (e: any) {
      throw new TransportError(`cannot open fd ${fd}: ${e.message}`);
    }
  }

  async write(data: Buffer): Promise<void> {
    if (!this._writeStream || !this._open) {
      throw new TransportError("not connected");
    }
    return new Promise((resolve, reject) => {
      this._writeStream!.write(data, (err) => {
        if (err) reject(new TransportError(`write error: ${err.message}`));
        else resolve();
      });
    });
  }

  async close(): Promise<void> {
    this._open = false;
    this._rl?.close();
    this._writeStream?.end();
  }
}

/** Windows Named Pipe transport. */
export class NamedPipeTransport extends BaseTransport {
  private _socket: Socket | null = null;

  async connect(pipeName: string): Promise<void> {
    // On Windows/Cygwin, named pipes are accessed via \\.\pipe\name
    const pipePath = pipeName.startsWith("\\\\.\\pipe\\")
      ? pipeName
      : `\\\\.\\pipe\\${pipeName}`;

    return new Promise((resolve, reject) => {
      const sock = createConnection({ path: pipePath }, () => {
        this._socket = sock;
        this._open = true;
        this.setupReadline(sock);
        resolve();
      });
      sock.on("error", (err) => {
        if (!this._open) {
          reject(new TransportError(`cannot connect to pipe ${pipeName}: ${err.message}`));
        } else {
          this._open = false;
        }
      });
    });
  }

  async write(data: Buffer): Promise<void> {
    if (!this._socket || !this._open) {
      throw new TransportError("not connected");
    }
    return new Promise((resolve, reject) => {
      this._socket!.write(data, (err) => {
        if (err) reject(new TransportError(`write error: ${err.message}`));
        else resolve();
      });
    });
  }

  async close(): Promise<void> {
    this._open = false;
    this._rl?.close();
    this._socket?.destroy();
  }
}
