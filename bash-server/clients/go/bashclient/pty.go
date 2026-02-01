package bashclient

import (
	"context"
	"encoding/base64"
	"sync"
)

// PtyChannel handles channel 5: pseudo-terminal I/O.
type PtyChannel struct {
	send sendFunc
	recv recvFunc

	mu       sync.Mutex
	onOutput func(string)
	onExit   func(int)
}

func newPtyChannel(send sendFunc, recv recvFunc) *PtyChannel {
	return &PtyChannel{send: send, recv: recv}
}

// Spawn creates a new PTY session.
func (c *PtyChannel) Spawn(ctx context.Context, opts *PtyOpts) (*PtyInfo, error) {
	rows, cols := 24, 80
	if opts != nil {
		if opts.Rows > 0 {
			rows = opts.Rows
		}
		if opts.Cols > 0 {
			cols = opts.Cols
		}
	}
	msg := MakeMsg(ChanPty, "spawn", map[string]any{
		"rows": rows, "cols": cols,
	})
	if opts != nil {
		if opts.Shell != "" {
			msg["shell"] = opts.Shell
		}
		if opts.StripAnsi {
			msg["strip_ansi"] = true
		}
	}
	if err := c.send(msg); err != nil {
		return nil, err
	}
	resp, err := c.recv(ctx, ChanPty)
	if err != nil {
		return nil, err
	}
	if getString(resp, "type") == "error" {
		return nil, newServerError(getString(resp, "message"), ChanPty)
	}
	return &PtyInfo{
		Rows:      getInt(resp, "rows"),
		Cols:      getInt(resp, "cols"),
		Pid:       getInt(resp, "pid"),
		StripAnsi: getBool(resp, "strip_ansi"),
	}, nil
}

// WriteInput sends input to the PTY (base64-encoded).
func (c *PtyChannel) WriteInput(ctx context.Context, data string) error {
	encoded := base64.StdEncoding.EncodeToString([]byte(data))
	return c.send(MakeMsg(ChanPty, "input", map[string]any{
		"data": encoded, "encoding": "base64",
	}))
}

// Resize changes the PTY dimensions.
func (c *PtyChannel) Resize(ctx context.Context, rows, cols int) error {
	if err := c.send(MakeMsg(ChanPty, "resize", map[string]any{
		"rows": rows, "cols": cols,
	})); err != nil {
		return err
	}
	resp, err := c.recv(ctx, ChanPty)
	if err != nil {
		return err
	}
	if getString(resp, "type") == "error" {
		return newServerError(getString(resp, "message"), ChanPty)
	}
	return nil
}

// Signal sends a signal to the PTY process.
func (c *PtyChannel) Signal(ctx context.Context, name string) error {
	if err := c.send(MakeMsg(ChanPty, "signal", map[string]any{
		"signal": name,
	})); err != nil {
		return err
	}
	resp, err := c.recv(ctx, ChanPty)
	if err != nil {
		return err
	}
	if getString(resp, "type") == "error" {
		return newServerError(getString(resp, "message"), ChanPty)
	}
	return nil
}

// Close closes the PTY session.
func (c *PtyChannel) Close(ctx context.Context) error {
	return c.send(MakeMsg(ChanPty, "close"))
}

// OnOutput registers a callback for PTY output. Pass nil to unregister.
func (c *PtyChannel) OnOutput(fn func(string)) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.onOutput = fn
}

// OnExit registers a callback for PTY exit. Pass nil to unregister.
func (c *PtyChannel) OnExit(fn func(int)) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.onExit = fn
}

// dispatch routes a server-push message to registered callbacks.
func (c *PtyChannel) dispatch(msg Message) {
	t := getString(msg, "type")

	c.mu.Lock()
	outCb := c.onOutput
	exitCb := c.onExit
	c.mu.Unlock()

	switch t {
	case "output":
		if outCb != nil {
			data := getString(msg, "data")
			if getString(msg, "encoding") == "base64" {
				decoded, err := base64.StdEncoding.DecodeString(data)
				if err == nil {
					data = string(decoded)
				}
			}
			outCb(data)
		}
	case "exit":
		if exitCb != nil {
			exitCb(getInt(msg, "exit_code"))
		}
	}
}
