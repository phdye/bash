/**
 * NDJSON streaming protocol for bash-server v2.
 */

import { ProtocolError } from "./errors";
import { FRAME_MAX_PAYLOAD, Message } from "./types";

/** Encode a message as NDJSON line (Buffer with trailing newline). */
export function encodeFrame(msg: Message): Buffer {
  try {
    return Buffer.from(JSON.stringify(msg) + "\n", "utf-8");
  } catch (e) {
    throw new ProtocolError(`cannot encode frame: ${e}`);
  }
}

/** Decode an NDJSON line into a Message. */
export function decodeFrame(line: string | Buffer): Message {
  const str = typeof line === "string" ? line.trim() : line.toString("utf-8").trim();
  if (!str) {
    throw new ProtocolError("empty frame");
  }
  if (str.length > FRAME_MAX_PAYLOAD) {
    throw new ProtocolError(
      `frame exceeds max payload (${str.length} > ${FRAME_MAX_PAYLOAD})`
    );
  }
  let msg: unknown;
  try {
    msg = JSON.parse(str);
  } catch (e) {
    throw new ProtocolError(`invalid JSON: ${e}`);
  }
  if (typeof msg !== "object" || msg === null || Array.isArray(msg)) {
    throw new ProtocolError(`expected JSON object, got ${typeof msg}`);
  }
  return msg as Message;
}

/** Base64 encode a string. */
export function b64encode(data: string): string {
  return Buffer.from(data, "utf-8").toString("base64");
}

/** Base64 decode a string. */
export function b64decode(data: string): string {
  try {
    return Buffer.from(data, "base64").toString("utf-8");
  } catch (e) {
    throw new ProtocolError(`base64 decode error: ${e}`);
  }
}

/** Build a protocol message. */
export function makeMsg(
  channel: number,
  type: string,
  extra?: Record<string, unknown>
): Message {
  const msg: Message = { ch: channel, type };
  if (extra) {
    Object.assign(msg, extra);
  }
  return msg;
}

/** Extract channel ID (defaults to 0). */
export function getChannel(msg: Message): number {
  return typeof msg.ch === "number" ? msg.ch : 0;
}

/** Extract message type. */
export function getType(msg: Message): string {
  if (typeof msg.type !== "string") {
    throw new ProtocolError("message has no 'type' field");
  }
  return msg.type;
}

/**
 * NDJSON streaming parser.
 * Buffers incoming data and emits complete JSON lines.
 */
export class NdjsonParser {
  private buffer = "";

  /** Feed data and return any complete messages. */
  feed(data: string | Buffer): Message[] {
    this.buffer += typeof data === "string" ? data : data.toString("utf-8");
    const messages: Message[] = [];
    let idx: number;
    while ((idx = this.buffer.indexOf("\n")) !== -1) {
      const line = this.buffer.slice(0, idx);
      this.buffer = this.buffer.slice(idx + 1);
      if (line.trim()) {
        try {
          messages.push(decodeFrame(line));
        } catch {
          // Skip malformed lines
        }
      }
    }
    return messages;
  }

  /** Reset parser state. */
  reset(): void {
    this.buffer = "";
  }
}
