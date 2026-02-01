package bashclient

import "context"

// StateChannel handles channel 2: variable, function, alias, and trap management.
type StateChannel struct {
	send sendFunc
	recv recvFunc
}

func newStateChannel(send sendFunc, recv recvFunc) *StateChannel {
	return &StateChannel{send: send, recv: recv}
}

func (c *StateChannel) request(ctx context.Context, msg Message) (Message, error) {
	if err := c.send(msg); err != nil {
		return nil, err
	}
	resp, err := c.recv(ctx, ChanState)
	if err != nil {
		return nil, err
	}
	if getString(resp, "type") == "error" {
		return nil, newServerError(getString(resp, "message"), ChanState)
	}
	return resp, nil
}

// GetVar retrieves a variable.
func (c *StateChannel) GetVar(ctx context.Context, name string) (*VarInfo, error) {
	resp, err := c.request(ctx, MakeMsg(ChanState, "get", map[string]any{
		"target": "var", "name": name,
	}))
	if err != nil {
		return nil, err
	}
	return &VarInfo{
		Name:       getString(resp, "name"),
		Value:      getString(resp, "value"),
		Attributes: getStringSlice(resp, "attributes"),
	}, nil
}

// SetVar sets a variable.
func (c *StateChannel) SetVar(ctx context.Context, name, value string, attributes []string) error {
	msg := MakeMsg(ChanState, "set", map[string]any{
		"target": "var", "name": name, "value": value,
	})
	if len(attributes) > 0 {
		msg["attributes"] = attributes
	}
	_, err := c.request(ctx, msg)
	return err
}

// UnsetVar removes a variable.
func (c *StateChannel) UnsetVar(ctx context.Context, name string) error {
	_, err := c.request(ctx, MakeMsg(ChanState, "unset", map[string]any{
		"target": "var", "name": name,
	}))
	return err
}

// GetFunc retrieves a function definition.
func (c *StateChannel) GetFunc(ctx context.Context, name string) (*FuncInfo, error) {
	resp, err := c.request(ctx, MakeMsg(ChanState, "get", map[string]any{
		"target": "function", "name": name,
	}))
	if err != nil {
		return nil, err
	}
	return &FuncInfo{
		Name:       getString(resp, "name"),
		Definition: getString(resp, "value"),
	}, nil
}

// UnsetFunc removes a function.
func (c *StateChannel) UnsetFunc(ctx context.Context, name string) error {
	_, err := c.request(ctx, MakeMsg(ChanState, "unset", map[string]any{
		"target": "function", "name": name,
	}))
	return err
}

// GetAlias retrieves an alias.
func (c *StateChannel) GetAlias(ctx context.Context, name string) (*AliasInfo, error) {
	resp, err := c.request(ctx, MakeMsg(ChanState, "get", map[string]any{
		"target": "alias", "name": name,
	}))
	if err != nil {
		return nil, err
	}
	return &AliasInfo{
		Name:  getString(resp, "name"),
		Value: getString(resp, "value"),
	}, nil
}

// SetAlias sets an alias.
func (c *StateChannel) SetAlias(ctx context.Context, name, value string) error {
	_, err := c.request(ctx, MakeMsg(ChanState, "set", map[string]any{
		"target": "alias", "name": name, "value": value,
	}))
	return err
}

// UnsetAlias removes an alias.
func (c *StateChannel) UnsetAlias(ctx context.Context, name string) error {
	_, err := c.request(ctx, MakeMsg(ChanState, "unset", map[string]any{
		"target": "alias", "name": name,
	}))
	return err
}

// SetTrap sets a trap handler.
func (c *StateChannel) SetTrap(ctx context.Context, signal, command string) error {
	_, err := c.request(ctx, MakeMsg(ChanState, "set", map[string]any{
		"target": "trap", "name": signal, "value": command,
	}))
	return err
}

// UnsetTrap removes a trap handler.
func (c *StateChannel) UnsetTrap(ctx context.Context, signal string) error {
	_, err := c.request(ctx, MakeMsg(ChanState, "unset", map[string]any{
		"target": "trap", "name": signal,
	}))
	return err
}

// Inspect queries state information.
func (c *StateChannel) Inspect(ctx context.Context, query string) ([]Message, error) {
	resp, err := c.request(ctx, MakeMsg(ChanState, "inspect", map[string]any{
		"query": query,
	}))
	if err != nil {
		return nil, err
	}
	items := getSlice(resp, "data")
	result := make([]Message, 0, len(items))
	for _, item := range items {
		if m, ok := item.(map[string]any); ok {
			result = append(result, Message(m))
		}
	}
	return result, nil
}
