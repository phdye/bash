package bashclient

import (
	"context"
	"sync"
)

// DebugChannel handles channel 4: debugging (breakpoints, stepping, AST).
type DebugChannel struct {
	send sendFunc
	recv recvFunc

	mu        sync.Mutex
	onBreakHit func(BreakHitEvent)
}

func newDebugChannel(send sendFunc, recv recvFunc) *DebugChannel {
	return &DebugChannel{send: send, recv: recv}
}

func (c *DebugChannel) request(ctx context.Context, msg Message) (Message, error) {
	if err := c.send(msg); err != nil {
		return nil, err
	}
	resp, err := c.recv(ctx, ChanDebug)
	if err != nil {
		return nil, err
	}
	if getString(resp, "type") == "error" {
		return nil, newServerError(getString(resp, "message"), ChanDebug)
	}
	return resp, nil
}

// Enable enables the debugger.
func (c *DebugChannel) Enable(ctx context.Context) error {
	_, err := c.request(ctx, MakeMsg(ChanDebug, "enable"))
	return err
}

// Disable disables the debugger.
func (c *DebugChannel) Disable(ctx context.Context) error {
	_, err := c.request(ctx, MakeMsg(ChanDebug, "disable"))
	return err
}

// Status returns the debugger status.
func (c *DebugChannel) Status(ctx context.Context) (*DebugStatus, error) {
	resp, err := c.request(ctx, MakeMsg(ChanDebug, "status"))
	if err != nil {
		return nil, err
	}
	return &DebugStatus{
		Active:      getBool(resp, "active"),
		Mode:        getString(resp, "mode"),
		Breakpoints: getInt(resp, "breakpoints"),
		Depth:       getInt(resp, "depth"),
	}, nil
}

// AddBreakpoint adds a breakpoint. Returns the breakpoint ID.
func (c *DebugChannel) AddBreakpoint(ctx context.Context, kind string, opts *BreakpointOpts) (int, error) {
	msg := MakeMsg(ChanDebug, "break", map[string]any{"kind": kind})
	if opts != nil {
		if opts.Pattern != "" {
			msg["pattern"] = opts.Pattern
		}
		if opts.Line != 0 {
			msg["line"] = opts.Line
		}
		if opts.Condition != "" {
			msg["condition"] = opts.Condition
		}
	}
	resp, err := c.request(ctx, msg)
	if err != nil {
		return -1, err
	}
	return getInt(resp, "id"), nil
}

// RemoveBreakpoint removes a breakpoint by ID.
func (c *DebugChannel) RemoveBreakpoint(ctx context.Context, id int) error {
	_, err := c.request(ctx, MakeMsg(ChanDebug, "delete", map[string]any{"id": id}))
	return err
}

// EnableBreakpoint enables a breakpoint by ID.
func (c *DebugChannel) EnableBreakpoint(ctx context.Context, id int) error {
	_, err := c.request(ctx, MakeMsg(ChanDebug, "enable_bp", map[string]any{"id": id}))
	return err
}

// DisableBreakpoint disables a breakpoint by ID.
func (c *DebugChannel) DisableBreakpoint(ctx context.Context, id int) error {
	_, err := c.request(ctx, MakeMsg(ChanDebug, "disable_bp", map[string]any{"id": id}))
	return err
}

// ListBreakpoints returns all breakpoints.
func (c *DebugChannel) ListBreakpoints(ctx context.Context) ([]Breakpoint, error) {
	resp, err := c.request(ctx, MakeMsg(ChanDebug, "list"))
	if err != nil {
		return nil, err
	}
	items := getSlice(resp, "data")
	result := make([]Breakpoint, 0, len(items))
	for _, item := range items {
		if m, ok := item.(map[string]any); ok {
			bp := Message(m)
			result = append(result, Breakpoint{
				ID:        getInt(bp, "id"),
				Kind:      getString(bp, "type"),
				Enabled:   getBool(bp, "enabled"),
				HitCount:  getInt(bp, "hit_count"),
				Pattern:   getString(bp, "pattern"),
				Line:      getInt(bp, "line"),
				Condition: getString(bp, "condition"),
			})
		}
	}
	return result, nil
}

// Continue resumes execution.
func (c *DebugChannel) Continue(ctx context.Context) error {
	_, err := c.request(ctx, MakeMsg(ChanDebug, "continue"))
	return err
}

// Step steps into the next command.
func (c *DebugChannel) Step(ctx context.Context) error {
	_, err := c.request(ctx, MakeMsg(ChanDebug, "step"))
	return err
}

// Next steps over to the next command.
func (c *DebugChannel) Next(ctx context.Context) error {
	_, err := c.request(ctx, MakeMsg(ChanDebug, "next"))
	return err
}

// Finish runs until the current function returns.
func (c *DebugChannel) Finish(ctx context.Context) error {
	_, err := c.request(ctx, MakeMsg(ChanDebug, "finish"))
	return err
}

// Skip skips the current command.
func (c *DebugChannel) Skip(ctx context.Context) error {
	_, err := c.request(ctx, MakeMsg(ChanDebug, "skip"))
	return err
}

// InspectAST returns the current AST.
func (c *DebugChannel) InspectAST(ctx context.Context) (Message, error) {
	resp, err := c.request(ctx, MakeMsg(ChanDebug, "inspect_ast"))
	if err != nil {
		return nil, err
	}
	data := getMap(resp, "data")
	if data != nil {
		return data, nil
	}
	return Message{}, nil
}

// OnBreakHit registers a callback for break_hit events. Pass nil to unregister.
func (c *DebugChannel) OnBreakHit(fn func(BreakHitEvent)) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.onBreakHit = fn
}

// dispatch routes a server-push message to the registered callback.
func (c *DebugChannel) dispatch(msg Message) {
	t := getString(msg, "type")
	if t != "break_hit" {
		return
	}

	c.mu.Lock()
	cb := c.onBreakHit
	c.mu.Unlock()

	if cb != nil {
		cb(BreakHitEvent{
			Line:    getInt(msg, "line"),
			Command: getString(msg, "command"),
			Depth:   getInt(msg, "depth"),
		})
	}
}
