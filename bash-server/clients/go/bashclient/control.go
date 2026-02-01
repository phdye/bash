package bashclient

import "context"

// ControlChannel handles channel 0: auth, ping, configure, disconnect.
type ControlChannel struct {
	send sendFunc
	recv recvFunc
}

func newControlChannel(send sendFunc, recv recvFunc) *ControlChannel {
	return &ControlChannel{send: send, recv: recv}
}

// Auth authenticates with the server.
func (c *ControlChannel) Auth(ctx context.Context, token string) (Message, error) {
	if err := c.send(MakeMsg(ChanControl, "auth", map[string]any{"token": token})); err != nil {
		return nil, err
	}
	resp, err := c.recv(ctx, ChanControl)
	if err != nil {
		return nil, err
	}
	t := getString(resp, "type")
	if t == "error" {
		return nil, newAuthError(getString(resp, "message"))
	}
	if t != "auth_ok" {
		return nil, newAuthError("unexpected auth response: " + t)
	}
	return resp, nil
}

// Ping sends a ping and waits for pong.
func (c *ControlChannel) Ping(ctx context.Context) error {
	if err := c.send(MakeMsg(ChanControl, "ping")); err != nil {
		return err
	}
	resp, err := c.recv(ctx, ChanControl)
	if err != nil {
		return err
	}
	if getString(resp, "type") != "pong" {
		return newServerError("expected pong, got "+getString(resp, "type"), ChanControl)
	}
	return nil
}

// Configure sends a configure message.
func (c *ControlChannel) Configure(ctx context.Context, opts map[string]any) (Message, error) {
	msg := MakeMsg(ChanControl, "configure", opts)
	if err := c.send(msg); err != nil {
		return nil, err
	}
	return c.recv(ctx, ChanControl)
}

// Disconnect sends a disconnect message.
func (c *ControlChannel) Disconnect(ctx context.Context) error {
	if err := c.send(MakeMsg(ChanControl, "disconnect")); err != nil {
		return err
	}
	// Server may close before responding
	c.recv(ctx, ChanControl)
	return nil
}
