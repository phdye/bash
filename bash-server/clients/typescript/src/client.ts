/**
 * BashClient: async client for bash-server v2 NDJSON protocol.
 */

import { ControlChannel } from "./channels/control";
import { CommandChannel } from "./channels/command";
import { StateChannel } from "./channels/state";
import { ObserveChannel } from "./channels/observe";
import { DebugChannel } from "./channels/debug";
import { PtyChannel } from "./channels/pty";
import { ProtocolError, TimeoutError, TransportError } from "./errors";
import { decodeFrame, encodeFrame, getChannel } from "./protocol";
import {
  Transport,
  UnixSocketTransport,
  StdioTransport,
  FdTransport,
  NamedPipeTransport,
} from "./transport";
import {
  CHAN_COMMAND, CHAN_CONTROL, CHAN_DEBUG, CHAN_OBSERVE, CHAN_PTY, CHAN_STATE,
  EvalResult, Message,
} from "./types";

interface QueueEntry {
  resolve: (msg: Message) => void;
  reject: (err: Error) => void;
  timer?: ReturnType<typeof setTimeout>;
}

export class BashClient {
  private transport: Transport;
  private channelQueues: Map<number, QueueEntry[]> = new Map();
  private readerRunning = false;
  private _authenticated = false;

  public readonly control: ControlChannel;
  public readonly command: CommandChannel;
  public readonly state: StateChannel;
  public readonly observe: ObserveChannel;
  public readonly debug: DebugChannel;
  public readonly pty: PtyChannel;

  constructor(transport: Transport) {
    this.transport = transport;
    for (let i = 0; i <= 5; i++) {
      this.channelQueues.set(i, []);
    }

    const sendFn = this.sendMsg.bind(this);
    const recvFn = this.recvMsg.bind(this);

    this.control = new ControlChannel(sendFn, recvFn);
    this.command = new CommandChannel(sendFn, recvFn);
    this.state = new StateChannel(sendFn, recvFn);
    this.observe = new ObserveChannel(sendFn, recvFn);
    this.debug = new DebugChannel(sendFn, recvFn);
    this.pty = new PtyChannel(sendFn, recvFn);
  }

  /** Connect via Unix domain socket. */
  static async connect(socketPath: string): Promise<BashClient> {
    const transport = new UnixSocketTransport();
    await transport.connect(socketPath);
    const client = new BashClient(transport);
    client.startReader();
    return client;
  }

  /** Connect via subprocess stdin/stdout. */
  static async connectStdio(...args: string[]): Promise<BashClient> {
    const transport = new StdioTransport();
    await transport.connectProcess(...args);
    const client = new BashClient(transport);
    client.startReader();
    return client;
  }

  /** Connect via inherited fd. */
  static async connectFd(fd: number): Promise<BashClient> {
    const transport = new FdTransport();
    await transport.connect(fd);
    const client = new BashClient(transport);
    client.startReader();
    return client;
  }

  /** Connect via Windows Named Pipe. */
  static async connectNamedPipe(pipeName: string): Promise<BashClient> {
    const transport = new NamedPipeTransport();
    await transport.connect(pipeName);
    const client = new BashClient(transport);
    client.startReader();
    return client;
  }

  /** Create from existing transport. */
  static fromTransport(transport: Transport): BashClient {
    const client = new BashClient(transport);
    client.startReader();
    return client;
  }

  private startReader(): void {
    this.readerRunning = true;
    this.readerLoop().catch(() => {});
  }

  private async readerLoop(): Promise<void> {
    while (this.readerRunning && this.transport.isOpen) {
      let line: string;
      try {
        line = await this.transport.readLine();
      } catch {
        break;
      }

      let msg: Message;
      try {
        msg = decodeFrame(line);
      } catch {
        continue;
      }

      const ch = getChannel(msg);
      const msgType = String(msg.type ?? "");

      // Server-push dispatch
      if (ch === CHAN_OBSERVE && (msgType === "pre_command" || msgType === "post_command")) {
        this.observe.dispatch(msg);
        continue;
      }
      if (ch === CHAN_DEBUG && msgType === "break_hit") {
        this.debug.dispatch(msg);
        continue;
      }
      if (ch === CHAN_PTY && (msgType === "output" || msgType === "exit")) {
        this.pty.dispatch(msg);
        continue;
      }

      // Request/response queue
      const queue = this.channelQueues.get(ch);
      if (queue && queue.length > 0) {
        const entry = queue.shift()!;
        if (entry.timer) clearTimeout(entry.timer);
        entry.resolve(msg);
      }
    }
    this.readerRunning = false;
  }

  private async sendMsg(msg: Message): Promise<void> {
    await this.transport.write(encodeFrame(msg));
  }

  private recvMsg(channel: number, timeout?: number): Promise<Message> {
    const ms = timeout ?? 30000;
    return new Promise<Message>((resolve, reject) => {
      const entry: QueueEntry = { resolve, reject };
      entry.timer = setTimeout(() => {
        const queue = this.channelQueues.get(channel);
        if (queue) {
          const idx = queue.indexOf(entry);
          if (idx !== -1) queue.splice(idx, 1);
        }
        reject(new TimeoutError(`timeout on channel ${channel}`));
      }, ms);
      this.channelQueues.get(channel)!.push(entry);
    });
  }

  /** Authenticate. */
  async auth(token: string): Promise<void> {
    await this.control.auth(token);
    this._authenticated = true;
  }

  /** Eval convenience wrapper. */
  async eval(command: string, timeout?: number): Promise<EvalResult> {
    return this.command.eval(command, { timeout });
  }

  /** Ping. */
  async ping(): Promise<void> {
    return this.control.ping();
  }

  /** Close the connection. */
  async close(): Promise<void> {
    this.readerRunning = false;
    try {
      await this.control.disconnect();
    } catch {}
    await this.transport.close();
    // Reject pending queue entries
    for (const [, queue] of this.channelQueues) {
      for (const entry of queue) {
        if (entry.timer) clearTimeout(entry.timer);
        entry.reject(new TransportError("connection closed"));
      }
      queue.length = 0;
    }
  }

  get isConnected(): boolean {
    return this.transport.isOpen;
  }

  get isAuthenticated(): boolean {
    return this._authenticated;
  }
}
