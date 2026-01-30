import { AuthError, ServerError } from "../errors";
import { makeMsg } from "../protocol";
import { CHAN_CONTROL, Message } from "../types";

type SendFn = (msg: Message) => Promise<void>;
type RecvFn = (ch: number) => Promise<Message>;

export class ControlChannel {
  constructor(private send: SendFn, private recv: RecvFn) {}

  async auth(token: string): Promise<Message> {
    await this.send(makeMsg(CHAN_CONTROL, "auth", { token }));
    const resp = await this.recv(CHAN_CONTROL);
    if (resp.type === "error") {
      throw new AuthError(String(resp.message ?? "authentication failed"));
    }
    if (resp.type !== "auth_ok") {
      throw new AuthError(`unexpected auth response: ${resp.type}`);
    }
    return resp;
  }

  async ping(): Promise<void> {
    await this.send(makeMsg(CHAN_CONTROL, "ping"));
    const resp = await this.recv(CHAN_CONTROL);
    if (resp.type !== "pong") {
      throw new ServerError(`expected pong, got ${resp.type}`);
    }
  }

  async configure(options: Record<string, unknown>): Promise<Message> {
    await this.send(makeMsg(CHAN_CONTROL, "configure", options));
    return await this.recv(CHAN_CONTROL);
  }

  async disconnect(): Promise<void> {
    await this.send(makeMsg(CHAN_CONTROL, "disconnect"));
    try {
      await this.recv(CHAN_CONTROL);
    } catch {
      // server may close before responding
    }
  }
}
