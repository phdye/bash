import { ServerError } from "../errors";
import { b64decode, makeMsg } from "../protocol";
import { CHAN_PTY, Message, PtyInfo } from "../types";

type SendFn = (msg: Message) => Promise<void>;
type RecvFn = (ch: number) => Promise<Message>;
type Callback = (...args: any[]) => void;

export class PtyChannel {
  private callbacks: Map<string, Callback[]> = new Map();

  constructor(private send: SendFn, private recv: RecvFn) {}

  async spawn(options?: {
    rows?: number;
    cols?: number;
    shell?: string;
    strip_ansi?: boolean;
  }): Promise<PtyInfo> {
    const rows = options?.rows ?? 24;
    const cols = options?.cols ?? 80;
    const extra: Record<string, unknown> = { rows, cols };
    if (options?.shell !== undefined) extra.shell = options.shell;
    if (options?.strip_ansi !== undefined) extra.strip_ansi = options.strip_ansi;
    await this.send(makeMsg(CHAN_PTY, "spawn", extra));
    const resp = await this.recv(CHAN_PTY);
    if (resp.type === "error") {
      throw new ServerError(String(resp.message ?? "pty spawn error"), CHAN_PTY);
    }
    return {
      rows: Number(resp.rows ?? rows),
      cols: Number(resp.cols ?? cols),
      pid: Number(resp.pid ?? 0),
      strip_ansi: Boolean(resp.strip_ansi),
    };
  }

  async writeInput(data: string): Promise<void> {
    const encoded = Buffer.from(data, "utf-8").toString("base64");
    await this.send(makeMsg(CHAN_PTY, "input", { data: encoded, encoding: "base64" }));
  }

  async resize(rows: number, cols: number): Promise<void> {
    await this.send(makeMsg(CHAN_PTY, "resize", { rows, cols }));
    const resp = await this.recv(CHAN_PTY);
    if (resp.type === "error") {
      throw new ServerError(String(resp.message ?? "resize error"), CHAN_PTY);
    }
  }

  async signal(name: string): Promise<void> {
    await this.send(makeMsg(CHAN_PTY, "signal", { signal: name }));
    const resp = await this.recv(CHAN_PTY);
    if (resp.type === "error") {
      throw new ServerError(String(resp.message ?? "signal error"), CHAN_PTY);
    }
  }

  async close(): Promise<void> {
    await this.send(makeMsg(CHAN_PTY, "close"));
  }

  on(event: string, callback: Callback): void {
    const list = this.callbacks.get(event) ?? [];
    list.push(callback);
    this.callbacks.set(event, list);
  }

  off(event: string, callback?: Callback): void {
    if (!callback) {
      this.callbacks.delete(event);
    } else {
      const list = this.callbacks.get(event) ?? [];
      this.callbacks.set(event, list.filter((cb) => cb !== callback));
    }
  }

  dispatch(msg: Message): void {
    const t = String(msg.type ?? "");
    if (t === "output") {
      const raw = String(msg.data ?? "");
      const data = msg.encoding === "base64" ? b64decode(raw) : raw;
      for (const cb of this.callbacks.get("output") ?? []) {
        cb(data);
      }
    } else if (t === "exit") {
      const code = Number(msg.exit_code ?? -1);
      for (const cb of this.callbacks.get("exit") ?? []) {
        cb(code);
      }
    } else {
      for (const cb of this.callbacks.get(t) ?? []) {
        cb(msg);
      }
    }
  }
}
