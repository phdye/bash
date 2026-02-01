package bashclient

import (
	"context"
	"sync"
	"time"
)

// sendFunc is the type for sending a message.
type sendFunc func(Message) error

// recvFunc is the type for receiving a message on a channel.
type recvFunc func(ctx context.Context, ch int) (Message, error)

// BashClient is the main client for bash-server v2 NDJSON protocol.
type BashClient struct {
	transport     Transport
	channelQueues [6]chan Message
	done          chan struct{}
	closeOnce     sync.Once
	authenticated bool

	// Public channel accessors.
	Control *ControlChannel
	Command *CommandChannel
	State   *StateChannel
	Observe *ObserveChannel
	Debug   *DebugChannel
	Pty     *PtyChannel
}

// Connect connects via Unix domain socket.
func Connect(ctx context.Context, socketPath string) (*BashClient, error) {
	transport, err := ConnectUnixSocket(ctx, socketPath)
	if err != nil {
		return nil, err
	}
	return FromTransport(transport), nil
}

// ConnectStdio connects via subprocess stdin/stdout (--stdio mode).
func ConnectStdio(ctx context.Context, args ...string) (*BashClient, error) {
	transport, err := ConnectStdioTransport(ctx, args...)
	if err != nil {
		return nil, err
	}
	return FromTransport(transport), nil
}

// ConnectFd connects via an inherited file descriptor.
func ConnectFd(ctx context.Context, fd int) (*BashClient, error) {
	transport, err := ConnectFdTransport(ctx, fd)
	if err != nil {
		return nil, err
	}
	return FromTransport(transport), nil
}

// ConnectNamedPipe connects via a Windows Named Pipe.
func ConnectNamedPipe(ctx context.Context, pipeName string) (*BashClient, error) {
	transport, err := ConnectNamedPipeTransport(ctx, pipeName)
	if err != nil {
		return nil, err
	}
	return FromTransport(transport), nil
}

// FromTransport creates a client from an existing transport and starts the reader.
func FromTransport(transport Transport) *BashClient {
	c := &BashClient{
		transport: transport,
		done:      make(chan struct{}),
	}
	for i := range c.channelQueues {
		c.channelQueues[i] = make(chan Message, 64)
	}

	c.Control = newControlChannel(c.send, c.recv)
	c.Command = newCommandChannel(c.send, c.recv)
	c.State = newStateChannel(c.send, c.recv)
	c.Observe = newObserveChannel(c.send, c.recv)
	c.Debug = newDebugChannel(c.send, c.recv)
	c.Pty = newPtyChannel(c.send, c.recv)

	go c.readerLoop()
	return c
}

// readerLoop reads messages and dispatches to channel queues or callbacks.
func (c *BashClient) readerLoop() {
	defer func() {
		// Drain signal: close all channel queues so blocked recv calls unblock
		for i := range c.channelQueues {
			close(c.channelQueues[i])
		}
	}()

	for {
		select {
		case <-c.done:
			return
		default:
		}

		if !c.transport.IsOpen() {
			return
		}

		line, err := c.transport.ReadLine()
		if err != nil {
			return
		}

		msg, err := DecodeFrame(line)
		if err != nil {
			continue
		}

		ch := GetChannel(msg)
		msgType := getString(msg, "type")

		// Server-push messages go to dispatch
		switch {
		case ch == ChanObserve && (msgType == "pre_command" || msgType == "post_command"):
			c.Observe.dispatch(msg)
		case ch == ChanDebug && msgType == "break_hit":
			c.Debug.dispatch(msg)
		case ch == ChanPty && (msgType == "output" || msgType == "exit"):
			c.Pty.dispatch(msg)
		default:
			// Request/response messages go to channel queue
			if ch >= 0 && ch <= ChanMax {
				select {
				case c.channelQueues[ch] <- msg:
				case <-c.done:
					return
				}
			}
		}
	}
}

// send sends a message to the server.
func (c *BashClient) send(msg Message) error {
	data, err := EncodeFrame(msg)
	if err != nil {
		return err
	}
	return c.transport.Write(data)
}

// recv receives the next message on a specific channel.
func (c *BashClient) recv(ctx context.Context, channel int) (Message, error) {
	select {
	case msg, ok := <-c.channelQueues[channel]:
		if !ok {
			return nil, newTransportError("connection closed")
		}
		return msg, nil
	case <-ctx.Done():
		return nil, newTimeoutError("timeout waiting for response on channel")
	case <-c.done:
		return nil, newTransportError("client closed")
	}
}

// Auth authenticates with the server.
func (c *BashClient) Auth(ctx context.Context, token string) error {
	_, err := c.Control.Auth(ctx, token)
	if err != nil {
		return err
	}
	c.authenticated = true
	return nil
}

// Eval evaluates a command (convenience wrapper).
func (c *BashClient) Eval(ctx context.Context, command string) (*EvalResult, error) {
	return c.Command.Eval(ctx, command)
}

// Ping pings the server.
func (c *BashClient) Ping(ctx context.Context) error {
	return c.Control.Ping(ctx)
}

// Close closes the connection gracefully. It sends a disconnect message
// with a short timeout before closing the transport.
func (c *BashClient) Close() error {
	var err error
	c.closeOnce.Do(func() {
		// Send disconnect with a short timeout (best-effort).
		ctx, cancel := context.WithTimeout(context.Background(), 500*time.Millisecond)
		_ = c.Control.Disconnect(ctx)
		cancel()

		// Signal reader to stop.
		close(c.done)

		// Close transport.
		err = c.transport.Close()
	})
	return err
}

// IsConnected returns true if the transport is open.
func (c *BashClient) IsConnected() bool {
	return c.transport.IsOpen()
}

// IsAuthenticated returns true if Auth() succeeded.
func (c *BashClient) IsAuthenticated() bool {
	return c.authenticated
}
