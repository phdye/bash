import { ControlChannel } from "../src/channels/control";
import { CommandChannel } from "../src/channels/command";
import { StateChannel } from "../src/channels/state";
import { ObserveChannel } from "../src/channels/observe";
import { DebugChannel } from "../src/channels/debug";
import { PtyChannel } from "../src/channels/pty";
import { AuthError, ServerError } from "../src/errors";
import { Message } from "../src/types";

/** Helper: create send/recv mocks with a response queue. */
function mockIO() {
  const sent: Message[] = [];
  const responses: Message[] = [];
  const send = async (msg: Message) => { sent.push(msg); };
  const recv = async (_ch: number) => {
    const r = responses.shift();
    if (!r) throw new Error("no response queued");
    return r;
  };
  return { sent, responses, send, recv };
}

describe("ControlChannel", () => {
  it("auth succeeds", async () => {
    const io = mockIO();
    io.responses.push({ ch: 0, type: "auth_ok", capabilities: ["state"] });
    const ch = new ControlChannel(io.send, io.recv);
    const resp = await ch.auth("token");
    expect(resp.type).toBe("auth_ok");
    expect(io.sent[0].type).toBe("auth");
  });

  it("auth fails", async () => {
    const io = mockIO();
    io.responses.push({ ch: 0, type: "error", message: "invalid token" });
    const ch = new ControlChannel(io.send, io.recv);
    await expect(ch.auth("bad")).rejects.toThrow(AuthError);
  });

  it("ping", async () => {
    const io = mockIO();
    io.responses.push({ ch: 0, type: "pong" });
    const ch = new ControlChannel(io.send, io.recv);
    await ch.ping();
  });
});

describe("CommandChannel", () => {
  it("eval returns result", async () => {
    const io = mockIO();
    io.responses.push(
      { ch: 1, type: "stdout", data: Buffer.from("hello\n").toString("base64"), encoding: "base64" },
      { ch: 1, type: "stderr", data: Buffer.from("").toString("base64"), encoding: "base64" },
      { ch: 1, type: "complete", exit_code: 0 },
    );
    const ch = new CommandChannel(io.send, io.recv);
    const result = await ch.eval("echo hello");
    expect(result.stdout).toBe("hello\n");
    expect(result.exit_code).toBe(0);
  });
});

describe("StateChannel", () => {
  it("getVar", async () => {
    const io = mockIO();
    io.responses.push({ ch: 2, type: "value", target: "var", name: "X", value: "1", attributes: [] });
    const ch = new StateChannel(io.send, io.recv);
    const v = await ch.getVar("X");
    expect(v.name).toBe("X");
    expect(v.value).toBe("1");
  });

  it("setVar", async () => {
    const io = mockIO();
    io.responses.push({ ch: 2, type: "set_ok", target: "var", name: "X" });
    const ch = new StateChannel(io.send, io.recv);
    await ch.setVar("X", "2");
  });

  it("inspect", async () => {
    const io = mockIO();
    io.responses.push({ ch: 2, type: "inspect_result", query: "vars", data: [{ name: "A", value: "1" }] });
    const ch = new StateChannel(io.send, io.recv);
    const items = await ch.inspect("vars");
    expect(items).toHaveLength(1);
  });
});

describe("ObserveChannel", () => {
  it("dispatches pre_command", () => {
    const io = mockIO();
    const ch = new ObserveChannel(io.send, io.recv);
    const events: any[] = [];
    ch.on("pre_command", (e: any) => events.push(e));
    ch.dispatch({
      ch: 3, type: "pre_command", seq: 1, timestamp: 100,
      data: { command: "echo", cwd: "/", line_number: 1, is_subshell: false, is_async: false },
    });
    expect(events).toHaveLength(1);
    expect(events[0].command).toBe("echo");
  });
});

describe("DebugChannel", () => {
  it("addBreakpoint", async () => {
    const io = mockIO();
    io.responses.push({ ch: 4, type: "break_ok", id: 1 });
    const ch = new DebugChannel(io.send, io.recv);
    const id = await ch.addBreakpoint({ kind: "command", pattern: "echo" });
    expect(id).toBe(1);
  });

  it("dispatches break_hit", () => {
    const io = mockIO();
    const ch = new DebugChannel(io.send, io.recv);
    const events: any[] = [];
    ch.on("break_hit", (e: any) => events.push(e));
    ch.dispatch({ ch: 4, type: "break_hit", line: 5, command: "echo", depth: 0 });
    expect(events).toHaveLength(1);
    expect(events[0].line).toBe(5);
  });
});

describe("PtyChannel", () => {
  it("dispatches output", () => {
    const io = mockIO();
    const ch = new PtyChannel(io.send, io.recv);
    const outputs: string[] = [];
    ch.on("output", (data: string) => outputs.push(data));
    ch.dispatch({
      ch: 5, type: "output",
      data: Buffer.from("hello").toString("base64"),
      encoding: "base64",
    });
    expect(outputs).toHaveLength(1);
    expect(outputs[0]).toBe("hello");
  });

  it("dispatches exit", () => {
    const io = mockIO();
    const ch = new PtyChannel(io.send, io.recv);
    const codes: number[] = [];
    ch.on("exit", (c: number) => codes.push(c));
    ch.dispatch({ ch: 5, type: "exit", exit_code: 42 });
    expect(codes).toEqual([42]);
  });
});
