package bashclient

import "fmt"

// BashClientError is the base error type for all bashclient errors.
type BashClientError struct {
	Message string
	Cause   error
}

func (e *BashClientError) Error() string { return e.Message }

// Unwrap returns the underlying cause, supporting errors.Is and errors.As.
func (e *BashClientError) Unwrap() error { return e.Cause }

// AuthError indicates authentication failure.
type AuthError struct {
	BashClientError
}

// ProtocolError indicates a malformed frame or message.
type ProtocolError struct {
	BashClientError
}

// TimeoutError indicates an operation timed out.
type TimeoutError struct {
	BashClientError
}

// TransportError indicates a socket or I/O error.
type TransportError struct {
	BashClientError
}

// ServerError indicates the server returned an error response.
type ServerError struct {
	BashClientError
	Channel int
}

// Error returns a formatted error string including the channel number.
func (e *ServerError) Error() string {
	return fmt.Sprintf("server error on channel %d: %s", e.Channel, e.Message)
}

func newAuthError(msg string) *AuthError {
	return &AuthError{BashClientError{Message: msg}}
}

func newProtocolError(msg string) *ProtocolError {
	return &ProtocolError{BashClientError{Message: msg, Cause: nil}}
}

func newProtocolErrorf(cause error, format string, args ...any) *ProtocolError {
	return &ProtocolError{BashClientError{Message: fmt.Sprintf(format, args...), Cause: cause}}
}

func newTimeoutError(msg string) *TimeoutError {
	return &TimeoutError{BashClientError{Message: msg}}
}

func newTransportError(msg string) *TransportError {
	return &TransportError{BashClientError{Message: msg, Cause: nil}}
}

func newTransportErrorf(cause error, format string, args ...any) *TransportError {
	return &TransportError{BashClientError{Message: fmt.Sprintf(format, args...), Cause: cause}}
}

func newServerError(msg string, ch int) *ServerError {
	return &ServerError{BashClientError: BashClientError{Message: msg}, Channel: ch}
}
