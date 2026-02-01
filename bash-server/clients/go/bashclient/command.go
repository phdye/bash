package bashclient

import "context"

// CommandChannel handles channel 1: command evaluation.
type CommandChannel struct {
	send sendFunc
	recv recvFunc
}

func newCommandChannel(send sendFunc, recv recvFunc) *CommandChannel {
	return &CommandChannel{send: send, recv: recv}
}

// Eval evaluates a command and returns the result.
func (c *CommandChannel) Eval(ctx context.Context, command string) (*EvalResult, error) {
	msg := MakeMsg(ChanCommand, "eval", map[string]any{"command": command})
	if err := c.send(msg); err != nil {
		return nil, err
	}

	result := &EvalResult{ExitCode: -1}

	// Collect stdout, stderr, complete messages
	for i := 0; i < 3; i++ {
		resp, err := c.recv(ctx, ChanCommand)
		if err != nil {
			return nil, err
		}
		t := getString(resp, "type")
		switch t {
		case "stdout":
			data := getString(resp, "data")
			if getString(resp, "encoding") == "base64" {
				decoded, err := B64Decode(data)
				if err != nil {
					return nil, err
				}
				data = decoded
			}
			result.Stdout = data
		case "stderr":
			data := getString(resp, "data")
			if getString(resp, "encoding") == "base64" {
				decoded, err := B64Decode(data)
				if err != nil {
					return nil, err
				}
				data = decoded
			}
			result.Stderr = data
		case "complete":
			result.ExitCode = getInt(resp, "exit_code")
		case "error":
			return nil, newServerError(getString(resp, "message"), ChanCommand)
		}
	}

	return result, nil
}
