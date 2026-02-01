import { ServerError } from "../errors";
import { makeMsg } from "../protocol";
import {
  Breakpoint, BreakHitEvent, CHAN_DEBUG, DebugStatus, Message,
} from "../types";

type SendFn = (msg: Message) => Promise<void>;
type RecvFn = (ch: number) => Promise<Message>;
type Callback = (...args: any[]) => void;

export class DebugChannel {
  private callbacks: Map<string, Callback[]> = new Map();

  constructor(private send: SendFn, private recv: RecvFn) {}

  private async request(msg: Message): Promise<Message> {
    await this.send(msg);
    const resp = await this.recv(CHAN_DEBUG);
    if (resp.type === "error") {
      throw new ServerError(String(resp.message ?? "debug error"), CHAN_DEBUG);
    }
    return resp;
  }

  async enable(): Promise<void> {
    await this.request(makeMsg(CHAN_DEBUG, "enable"));
  }

  async disable(): Promise<void> {
    await this.request(makeMsg(CHAN_DEBUG, "disable"));
  }

  async status(): Promise<DebugStatus> {
    const resp = await this.request(makeMsg(CHAN_DEBUG, "status"));
    return {
      active: Boolean(resp.active),
      mode: String(resp.mode ?? "run"),
      breakpoints: Number(resp.breakpoints ?? 0),
      depth: Number(resp.depth ?? 0),
    };
  }

  async addBreakpoint(options: {
    kind: string;
    pattern?: string;
    line?: number;
    condition?: string;
  }): Promise<number> {
    const extra: Record<string, unknown> = { kind: options.kind };
    if (options.pattern !== undefined) extra.pattern = options.pattern;
    if (options.line !== undefined) extra.line = options.line;
    if (options.condition !== undefined) extra.condition = options.condition;
    const resp = await this.request(makeMsg(CHAN_DEBUG, "break", extra));
    return Number(resp.id ?? -1);
  }

  async removeBreakpoint(id: number): Promise<boolean> {
    const resp = await this.request(makeMsg(CHAN_DEBUG, "delete", { id }));
    return Boolean(resp.found);
  }

  async enableBreakpoint(id: number): Promise<boolean> {
    const resp = await this.request(makeMsg(CHAN_DEBUG, "enable_bp", { id }));
    return Boolean(resp.found);
  }

  async disableBreakpoint(id: number): Promise<boolean> {
    const resp = await this.request(makeMsg(CHAN_DEBUG, "disable_bp", { id }));
    return Boolean(resp.found);
  }

  async listBreakpoints(): Promise<Breakpoint[]> {
    const resp = await this.request(makeMsg(CHAN_DEBUG, "list"));
    return ((resp.data as any[]) ?? []).map((bp) => ({
      id: Number(bp.id ?? 0),
      kind: String(bp.type ?? ""),
      enabled: Boolean(bp.enabled ?? true),
      hit_count: Number(bp.hit_count ?? 0),
      pattern: bp.pattern as string | undefined,
      line: bp.line as number | undefined,
      condition: bp.condition as string | undefined,
    }));
  }

  async continue_(): Promise<void> {
    await this.request(makeMsg(CHAN_DEBUG, "continue"));
  }

  async step(): Promise<void> {
    await this.request(makeMsg(CHAN_DEBUG, "step"));
  }

  async next(): Promise<void> {
    await this.request(makeMsg(CHAN_DEBUG, "next"));
  }

  async finish(): Promise<void> {
    await this.request(makeMsg(CHAN_DEBUG, "finish"));
  }

  async skip(): Promise<void> {
    await this.request(makeMsg(CHAN_DEBUG, "skip"));
  }

  async inspectAst(): Promise<Record<string, unknown>> {
    const resp = await this.request(makeMsg(CHAN_DEBUG, "inspect_ast"));
    return (resp.data as Record<string, unknown>) ?? {};
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
    let event: BreakHitEvent | Message;
    if (t === "break_hit") {
      event = {
        line: Number(msg.line ?? 0),
        command: String(msg.command ?? ""),
        depth: Number(msg.depth ?? 0),
      };
    } else {
      event = msg;
    }
    for (const cb of this.callbacks.get(t) ?? []) {
      cb(event);
    }
  }
}
