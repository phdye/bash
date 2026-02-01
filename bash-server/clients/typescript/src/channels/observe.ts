import { ServerError } from "../errors";
import { makeMsg } from "../protocol";
import {
  CHAN_OBSERVE, Message, PreCommandEvent, PostCommandEvent,
} from "../types";

type SendFn = (msg: Message) => Promise<void>;
type RecvFn = (ch: number) => Promise<Message>;
type Callback = (...args: any[]) => void;

export class ObserveChannel {
  private callbacks: Map<string, Callback[]> = new Map();

  constructor(private send: SendFn, private recv: RecvFn) {}

  async subscribe(level: number = 1): Promise<void> {
    await this.send(makeMsg(CHAN_OBSERVE, "subscribe", { level }));
    const resp = await this.recv(CHAN_OBSERVE);
    if (resp.type === "error") {
      throw new ServerError(String(resp.message ?? "subscribe error"), CHAN_OBSERVE);
    }
  }

  async unsubscribe(): Promise<void> {
    await this.send(makeMsg(CHAN_OBSERVE, "unsubscribe"));
    const resp = await this.recv(CHAN_OBSERVE);
    if (resp.type === "error") {
      throw new ServerError(String(resp.message ?? "unsubscribe error"), CHAN_OBSERVE);
    }
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

  /** Dispatch a server-push message. */
  dispatch(msg: Message): void {
    const t = String(msg.type ?? "");
    const data = (msg.data ?? {}) as Record<string, unknown>;

    let event: PreCommandEvent | PostCommandEvent | Message;
    if (t === "pre_command") {
      event = {
        seq: Number(msg.seq ?? 0),
        timestamp: Number(msg.timestamp ?? 0),
        command: String(data.command ?? ""),
        cwd: String(data.cwd ?? ""),
        line_number: Number(data.line_number ?? 0),
        is_subshell: Boolean(data.is_subshell),
        is_async: Boolean(data.is_async),
      };
    } else if (t === "post_command") {
      event = {
        seq: Number(msg.seq ?? 0),
        timestamp: Number(msg.timestamp ?? 0),
        command: String(data.command ?? ""),
        exit_status: Number(data.exit_status ?? 0),
        signal_number: Number(data.signal_number ?? 0),
        duration_ms: Number(data.duration_ms ?? 0),
      };
    } else {
      event = msg;
    }

    for (const cb of this.callbacks.get(t) ?? []) {
      cb(event);
    }
  }
}
