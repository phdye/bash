"""Exception hierarchy for bashclient."""


class BashClientError(Exception):
    """Base exception for all bashclient errors."""


class AuthError(BashClientError):
    """Authentication failed."""


class ProtocolError(BashClientError):
    """Malformed frame or message."""


class TimeoutError(BashClientError):
    """Operation timed out."""


class TransportError(BashClientError):
    """Socket or I/O error."""


class ServerError(BashClientError):
    """Server returned an error response."""

    def __init__(self, message: str, channel: int = -1):
        super().__init__(message)
        self.channel = channel
