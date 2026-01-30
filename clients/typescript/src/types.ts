/** Channel IDs */
export const CHAN_CONTROL = 0;
export const CHAN_COMMAND = 1;
export const CHAN_STATE = 2;
export const CHAN_OBSERVE = 3;
export const CHAN_DEBUG = 4;
export const CHAN_PTY = 5;
export const CHAN_MAX = 5;

/** Protocol limits */
export const FRAME_MAX_PAYLOAD = 1024 * 1024; // 1MB
export const TOKEN_HEXLEN = 64;

/** Observe levels */
export const OBSERVE_LEVEL_OFF = 0;
export const OBSERVE_LEVEL_COMMAND = 1;

/** Generic protocol message */
export interface Message {
  ch: number;
  type: string;
  [key: string]: unknown;
}

/** Command evaluation result */
export interface EvalResult {
  stdout: string;
  stderr: string;
  exit_code: number;
}

/** Variable info */
export interface VarInfo {
  name: string;
  value: string;
  attributes: string[];
}

/** Function info */
export interface FuncInfo {
  name: string;
  definition: string;
}

/** Alias info */
export interface AliasInfo {
  name: string;
  value: string;
}

/** Trap info */
export interface TrapInfo {
  signal: string;
  command: string;
}

/** Pre-command observation event */
export interface PreCommandEvent {
  seq: number;
  timestamp: number;
  command: string;
  cwd: string;
  line_number: number;
  is_subshell: boolean;
  is_async: boolean;
}

/** Post-command observation event */
export interface PostCommandEvent {
  seq: number;
  timestamp: number;
  command: string;
  exit_status: number;
  signal_number: number;
  duration_ms: number;
}

/** Breakpoint */
export interface Breakpoint {
  id: number;
  kind: string;
  enabled: boolean;
  hit_count: number;
  pattern?: string;
  line?: number;
  condition?: string;
}

/** Break hit event */
export interface BreakHitEvent {
  line: number;
  command: string;
  depth: number;
}

/** Debug status */
export interface DebugStatus {
  active: boolean;
  mode: string;
  breakpoints: number;
  depth: number;
}

/** PTY session info */
export interface PtyInfo {
  rows: number;
  cols: number;
  pid: number;
  strip_ansi: boolean;
}

/** Inspect result item */
export interface InspectItem {
  name: string;
  value?: string;
  definition?: string;
  attributes?: string[];
  signal?: string;
  command?: string;
}
