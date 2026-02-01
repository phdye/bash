import {
  encodeFrame, decodeFrame, b64encode, b64decode,
  makeMsg, getChannel, getType, NdjsonParser,
} from "../src/protocol";
import { ProtocolError } from "../src/errors";
import { FRAME_MAX_PAYLOAD } from "../src/types";

describe("encodeFrame", () => {
  it("encodes a message with trailing newline", () => {
    const buf = encodeFrame({ ch: 0, type: "ping" });
    expect(buf.toString("utf-8")).toMatch(/\n$/);
    expect(JSON.parse(buf.toString())).toEqual({ ch: 0, type: "ping" });
  });

  it("throws on non-serializable input", () => {
    const circular: any = { ch: 0, type: "x" };
    circular.self = circular;
    expect(() => encodeFrame(circular)).toThrow(ProtocolError);
  });
});

describe("decodeFrame", () => {
  it("decodes a valid JSON line", () => {
    const msg = decodeFrame('{"ch":0,"type":"pong"}\n');
    expect(msg).toEqual({ ch: 0, type: "pong" });
  });

  it("strips whitespace", () => {
    const msg = decodeFrame('  {"ch":1,"type":"eval"}  \n');
    expect(msg.ch).toBe(1);
  });

  it("throws on empty", () => {
    expect(() => decodeFrame("")).toThrow(ProtocolError);
  });

  it("throws on invalid JSON", () => {
    expect(() => decodeFrame("not json\n")).toThrow(ProtocolError);
  });

  it("throws on non-object", () => {
    expect(() => decodeFrame("[1,2,3]\n")).toThrow(ProtocolError);
  });

  it("throws on oversized", () => {
    const huge = "{" + "x".repeat(FRAME_MAX_PAYLOAD + 1) + "}";
    expect(() => decodeFrame(huge)).toThrow(ProtocolError);
  });
});

describe("base64", () => {
  it("roundtrips ASCII", () => {
    expect(b64decode(b64encode("hello world"))).toBe("hello world");
  });

  it("roundtrips unicode", () => {
    const text = "日本語テスト";
    expect(b64decode(b64encode(text))).toBe(text);
  });
});

describe("makeMsg", () => {
  it("builds a message", () => {
    expect(makeMsg(0, "auth", { token: "abc" })).toEqual({
      ch: 0, type: "auth", token: "abc",
    });
  });
});

describe("getChannel", () => {
  it("returns ch field", () => {
    expect(getChannel({ ch: 3, type: "x" })).toBe(3);
  });

  it("defaults to 0", () => {
    expect(getChannel({ type: "x" } as any)).toBe(0);
  });
});

describe("getType", () => {
  it("returns type field", () => {
    expect(getType({ ch: 0, type: "ping" })).toBe("ping");
  });

  it("throws if missing", () => {
    expect(() => getType({ ch: 0 } as any)).toThrow(ProtocolError);
  });
});

describe("NdjsonParser", () => {
  it("parses complete lines", () => {
    const parser = new NdjsonParser();
    const msgs = parser.feed('{"ch":0,"type":"a"}\n{"ch":1,"type":"b"}\n');
    expect(msgs).toHaveLength(2);
    expect(msgs[0].type).toBe("a");
    expect(msgs[1].type).toBe("b");
  });

  it("buffers partial lines", () => {
    const parser = new NdjsonParser();
    expect(parser.feed('{"ch":0,')).toHaveLength(0);
    const msgs = parser.feed('"type":"x"}\n');
    expect(msgs).toHaveLength(1);
    expect(msgs[0].type).toBe("x");
  });

  it("skips malformed lines", () => {
    const parser = new NdjsonParser();
    const msgs = parser.feed('bad json\n{"ch":0,"type":"ok"}\n');
    expect(msgs).toHaveLength(1);
    expect(msgs[0].type).toBe("ok");
  });
});
