export { BashClient } from "./client";

export {
  AuthError,
  BashClientError,
  ProtocolError,
  ServerError,
  TimeoutError,
  TransportError,
} from "./errors";

export {
  encodeFrame,
  decodeFrame,
  b64encode,
  b64decode,
  makeMsg,
  getChannel,
  getType,
  NdjsonParser,
} from "./protocol";

export {
  Transport,
  UnixSocketTransport,
  StdioTransport,
  FdTransport,
  NamedPipeTransport,
} from "./transport";

export { ControlChannel } from "./channels/control";
export { CommandChannel } from "./channels/command";
export { StateChannel } from "./channels/state";
export { ObserveChannel } from "./channels/observe";
export { DebugChannel } from "./channels/debug";
export { PtyChannel } from "./channels/pty";

export {
  CHAN_CONTROL, CHAN_COMMAND, CHAN_STATE, CHAN_OBSERVE, CHAN_DEBUG, CHAN_PTY,
  CHAN_MAX, FRAME_MAX_PAYLOAD, TOKEN_HEXLEN,
  OBSERVE_LEVEL_OFF, OBSERVE_LEVEL_COMMAND,
} from "./types";

export type {
  Message, EvalResult, VarInfo, FuncInfo, AliasInfo, TrapInfo,
  PreCommandEvent, PostCommandEvent,
  Breakpoint, BreakHitEvent, DebugStatus,
  PtyInfo, InspectItem,
} from "./types";
