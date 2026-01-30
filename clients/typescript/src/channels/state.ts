import { ServerError } from "../errors";
import { makeMsg } from "../protocol";
import {
  AliasInfo, CHAN_STATE, FuncInfo, InspectItem, Message, VarInfo,
} from "../types";

type SendFn = (msg: Message) => Promise<void>;
type RecvFn = (ch: number) => Promise<Message>;

export class StateChannel {
  constructor(private send: SendFn, private recv: RecvFn) {}

  private async request(msg: Message): Promise<Message> {
    await this.send(msg);
    const resp = await this.recv(CHAN_STATE);
    if (resp.type === "error") {
      throw new ServerError(String(resp.message ?? "state error"), CHAN_STATE);
    }
    return resp;
  }

  // Variables
  async getVar(name: string): Promise<VarInfo> {
    const resp = await this.request(makeMsg(CHAN_STATE, "get", { target: "var", name }));
    return {
      name: String(resp.name ?? name),
      value: String(resp.value ?? ""),
      attributes: (resp.attributes as string[]) ?? [],
    };
  }

  async setVar(name: string, value: string, attributes?: string[]): Promise<void> {
    const extra: Record<string, unknown> = { target: "var", name, value };
    if (attributes) extra.attributes = attributes;
    await this.request(makeMsg(CHAN_STATE, "set", extra));
  }

  async unsetVar(name: string): Promise<void> {
    await this.request(makeMsg(CHAN_STATE, "unset", { target: "var", name }));
  }

  // Functions
  async getFunc(name: string): Promise<FuncInfo> {
    const resp = await this.request(makeMsg(CHAN_STATE, "get", { target: "function", name }));
    return { name: String(resp.name ?? name), definition: String(resp.value ?? "") };
  }

  async unsetFunc(name: string): Promise<void> {
    await this.request(makeMsg(CHAN_STATE, "unset", { target: "function", name }));
  }

  // Aliases
  async getAlias(name: string): Promise<AliasInfo> {
    const resp = await this.request(makeMsg(CHAN_STATE, "get", { target: "alias", name }));
    return { name: String(resp.name ?? name), value: String(resp.value ?? "") };
  }

  async setAlias(name: string, value: string): Promise<void> {
    await this.request(makeMsg(CHAN_STATE, "set", { target: "alias", name, value }));
  }

  async unsetAlias(name: string): Promise<void> {
    await this.request(makeMsg(CHAN_STATE, "unset", { target: "alias", name }));
  }

  // Traps
  async setTrap(signal: string, command: string): Promise<void> {
    await this.request(makeMsg(CHAN_STATE, "set", { target: "trap", name: signal, value: command }));
  }

  async unsetTrap(signal: string): Promise<void> {
    await this.request(makeMsg(CHAN_STATE, "unset", { target: "trap", name: signal }));
  }

  // Inspect
  async inspect(query: string): Promise<InspectItem[]> {
    const resp = await this.request(makeMsg(CHAN_STATE, "inspect", { query }));
    return (resp.data as InspectItem[]) ?? [];
  }
}
