import { ServerError } from "../errors";
import { b64decode, makeMsg } from "../protocol";
import { CHAN_COMMAND, EvalResult, Message } from "../types";

type SendFn = (msg: Message) => Promise<void>;
type RecvFn = (ch: number, timeout?: number) => Promise<Message>;

export class CommandChannel {
  constructor(private send: SendFn, private recv: RecvFn) {}

  async eval(command: string, options?: { id?: string; timeout?: number }): Promise<EvalResult> {
    const msg = makeMsg(CHAN_COMMAND, "eval", { command });
    if (options?.id) (msg as any).id = options.id;
    await this.send(msg);

    let stdout = "";
    let stderr = "";
    let exit_code = -1;
    const timeout = options?.timeout ?? 30000;

    for (let i = 0; i < 3; i++) {
      const resp = await this.recv(CHAN_COMMAND, timeout);
      switch (resp.type) {
        case "stdout": {
          const data = String(resp.data ?? "");
          stdout = resp.encoding === "base64" ? b64decode(data) : data;
          break;
        }
        case "stderr": {
          const data = String(resp.data ?? "");
          stderr = resp.encoding === "base64" ? b64decode(data) : data;
          break;
        }
        case "complete":
          exit_code = Number(resp.exit_code ?? -1);
          break;
        case "error":
          throw new ServerError(String(resp.message ?? "eval error"), CHAN_COMMAND);
      }
    }

    return { stdout, stderr, exit_code };
  }
}
