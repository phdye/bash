package bashclient

import "fmt"

// BashClientError is the base error type for all bashclient errors.
type BashClientError struct {
	Message string
}

func (e *BashClientError) Error() string { return e.Message }

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

func (e *ServerError) Error() string {
	return fmt.Sprintf("server error on channel %d: %s", e.Channel, e.Message)
}

func newAuthError(msg string) *AuthError {
	return &AuthError{BashClientError{Message: msg}}
}

func newProtocolError(msg string) *ProtocolError {
	return &ProtocolError{BashClientError{Message: msg}}
}

func newTimeoutError(msg string) *TimeoutError {
	return &TimeoutError{BashClientError{Message: msg}}
}

func newTransportError(msg string) *TransportError {
	return &TransportError{BashClientError{Message: msg}}
}

func newServerError(msg string, ch int) *ServerError {
	return &ServerError{BashClientError: BashClientError{Message: msg}, Channel: ch}
}
