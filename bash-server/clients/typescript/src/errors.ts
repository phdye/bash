/** Base error for bashclient */
export class BashClientError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "BashClientError";
  }
}

/** Authentication failed */
export class AuthError extends BashClientError {
  constructor(message: string = "authentication failed") {
    super(message);
    this.name = "AuthError";
  }
}

/** Malformed frame or message */
export class ProtocolError extends BashClientError {
  constructor(message: string) {
    super(message);
    this.name = "ProtocolError";
  }
}

/** Operation timed out */
export class TimeoutError extends BashClientError {
  constructor(message: string = "operation timed out") {
    super(message);
    this.name = "TimeoutError";
  }
}

/** Socket or I/O error */
export class TransportError extends BashClientError {
  constructor(message: string) {
    super(message);
    this.name = "TransportError";
  }
}

/** Server returned error response */
export class ServerError extends BashClientError {
  public readonly channel: number;
  constructor(message: string, channel: number = -1) {
    super(message);
    this.name = "ServerError";
    this.channel = channel;
  }
}
