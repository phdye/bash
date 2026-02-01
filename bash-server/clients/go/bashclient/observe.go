package bashclient

import (
	"context"
	"sync"
)

// ObserveChannel handles channel 3: command observation events.
type ObserveChannel struct {
	send sendFunc
	recv recvFunc

	mu          sync.Mutex
	onPreCmd    func(PreCommandEvent)
	onPostCmd   func(PostCommandEvent)
}

func newObserveChannel(send sendFunc, recv recvFunc) *ObserveChannel {
	return &ObserveChannel{send: send, recv: recv}
}

// Subscribe subscribes to command observation events at the given level.
func (c *ObserveChannel) Subscribe(ctx context.Context, level int) error {
	if err := c.send(MakeMsg(ChanObserve, "subscribe", map[string]any{"level": level})); err != nil {
		return err
	}
	resp, err := c.recv(ctx, ChanObserve)
	if err != nil {
		return err
	}
	if getString(resp, "type") == "error" {
		return newServerError(getString(resp, "message"), ChanObserve)
	}
	return nil
}

// Unsubscribe stops observation.
func (c *ObserveChannel) Unsubscribe(ctx context.Context) error {
	if err := c.send(MakeMsg(ChanObserve, "unsubscribe")); err != nil {
		return err
	}
	resp, err := c.recv(ctx, ChanObserve)
	if err != nil {
		return err
	}
	if getString(resp, "type") == "error" {
		return newServerError(getString(resp, "message"), ChanObserve)
	}
	return nil
}

// OnPreCommand registers a callback for pre_command events. Pass nil to unregister.
func (c *ObserveChannel) OnPreCommand(fn func(PreCommandEvent)) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.onPreCmd = fn
}

// OnPostCommand registers a callback for post_command events. Pass nil to unregister.
func (c *ObserveChannel) OnPostCommand(fn func(PostCommandEvent)) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.onPostCmd = fn
}

// dispatch routes a server-push message to registered callbacks.
func (c *ObserveChannel) dispatch(msg Message) {
	t := getString(msg, "type")
	data := getMap(msg, "data")
	if data == nil {
		data = msg
	}

	c.mu.Lock()
	preCb := c.onPreCmd
	postCb := c.onPostCmd
	c.mu.Unlock()

	switch t {
	case "pre_command":
		if preCb != nil {
			preCb(PreCommandEvent{
				Seq:        getInt(msg, "seq"),
				Timestamp:  getInt64(msg, "timestamp"),
				Command:    getString(data, "command"),
				Cwd:        getString(data, "cwd"),
				LineNumber: getInt(data, "line_number"),
				IsSubshell: getBool(data, "is_subshell"),
				IsAsync:    getBool(data, "is_async"),
			})
		}
	case "post_command":
		if postCb != nil {
			postCb(PostCommandEvent{
				Seq:          getInt(msg, "seq"),
				Timestamp:    getInt64(msg, "timestamp"),
				Command:      getString(data, "command"),
				ExitStatus:   getInt(data, "exit_status"),
				SignalNumber: getInt(data, "signal_number"),
				DurationMs:   getInt(data, "duration_ms"),
			})
		}
	}
}
