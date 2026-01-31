package bashclient

import (
	"context"
	"encoding/base64"
	"encoding/json"
	"errors"
	"testing"
	"time"
)

// mockServer reads requests from serverT and injects responses.
func mockServer(t *testing.T, serverT *PipeTransport, token string) {
	t.Helper()
	for {
		line, err := serverT.ReadLine()
		if err != nil {
			return
		}
		var msg Message
		if err := json.Unmarshal(line, &msg); err != nil {
			continue
		}
		ch := GetChannel(msg)
		tp := getString(msg, "type")

		var responses []Message

		switch {
		case ch == ChanControl && tp == "auth":
			if getString(msg, "token") == token {
				responses = append(responses, Message{"ch": 0, "type": "auth_ok"})
			} else {
				responses = append(responses, Message{"ch": 0, "type": "error", "message": "invalid token"})
			}
		case ch == ChanControl && tp == "ping":
			responses = append(responses, Message{"ch": 0, "type": "pong"})
		case ch == ChanControl && tp == "disconnect":
			responses = append(responses, Message{"ch": 0, "type": "disconnect_ok"})
			// Send response then exit
			for _, r := range responses {
				data, _ := json.Marshal(r)
				serverT.Write(append(data, '\n'))
			}
			return
		case ch == ChanCommand && tp == "eval":
			cmd := getString(msg, "command")
			stdout := "mock: " + cmd + "\n"
			responses = append(responses,
				Message{"ch": 1, "type": "stdout", "data": base64.StdEncoding.EncodeToString([]byte(stdout)), "encoding": "base64"},
				Message{"ch": 1, "type": "stderr", "data": base64.StdEncoding.EncodeToString([]byte("")), "encoding": "base64"},
				Message{"ch": 1, "type": "complete", "exit_code": 0},
			)
		case ch == ChanState && tp == "get":
			target := getString(msg, "target")
			name := getString(msg, "name")
			switch target {
			case "var":
				responses = append(responses, Message{"ch": 2, "type": "value", "target": "var", "name": name, "value": "mock_" + name, "attributes": []any{}})
			case "function":
				responses = append(responses, Message{"ch": 2, "type": "value", "target": "function", "name": name, "value": name + " () { echo mock; }"})
			case "alias":
				responses = append(responses, Message{"ch": 2, "type": "value", "target": "alias", "name": name, "value": "mock_" + name})
			default:
				responses = append(responses, Message{"ch": 2, "type": "error", "message": "unknown target"})
			}
		case ch == ChanState && (tp == "set" || tp == "unset"):
			target := getString(msg, "target")
			name := getString(msg, "name")
			responses = append(responses, Message{"ch": 2, "type": tp + "_ok", "target": target, "name": name})
		case ch == ChanState && tp == "inspect":
			responses = append(responses, Message{"ch": 2, "type": "inspect_result", "data": []any{map[string]any{"name": "MOCK", "value": "1"}}})
		}

		for _, r := range responses {
			data, _ := json.Marshal(r)
			serverT.Write(append(data, '\n'))
		}
	}
}

func setupTestClient(t *testing.T) (*BashClient, func()) {
	t.Helper()
	clientT, serverT := NewPipeTransport()
	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	go mockServer(t, serverT, token)
	client := FromTransport(clientT)
	cleanup := func() {
		client.Close()
		serverT.Close()
	}
	return client, cleanup
}

func TestAuthSuccess(t *testing.T) {
	client, cleanup := setupTestClient(t)
	defer cleanup()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	err := client.Auth(ctx, token)
	if err != nil {
		t.Fatal(err)
	}
	if !client.IsAuthenticated() {
		t.Error("expected authenticated")
	}
}

func TestAuthFailure(t *testing.T) {
	client, cleanup := setupTestClient(t)
	defer cleanup()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	err := client.Auth(ctx, "wrong_token")
	if err == nil {
		t.Fatal("expected auth error")
	}
	var authErr *AuthError
	if !errors.As(err, &authErr) {
		t.Errorf("expected AuthError, got %T: %v", err, err)
	}
}

func TestPing(t *testing.T) {
	client, cleanup := setupTestClient(t)
	defer cleanup()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	client.Auth(ctx, token)

	if err := client.Ping(ctx); err != nil {
		t.Fatal(err)
	}
}

func TestEval(t *testing.T) {
	client, cleanup := setupTestClient(t)
	defer cleanup()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	client.Auth(ctx, token)

	result, err := client.Eval(ctx, "echo hello")
	if err != nil {
		t.Fatal(err)
	}
	if result.ExitCode != 0 {
		t.Errorf("expected exit_code=0, got %d", result.ExitCode)
	}
	if result.Stdout != "mock: echo hello\n" {
		t.Errorf("unexpected stdout: %q", result.Stdout)
	}
}

func TestGetVar(t *testing.T) {
	client, cleanup := setupTestClient(t)
	defer cleanup()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	client.Auth(ctx, token)

	info, err := client.State.GetVar(ctx, "PATH")
	if err != nil {
		t.Fatal(err)
	}
	if info.Name != "PATH" {
		t.Errorf("expected name=PATH, got %s", info.Name)
	}
	if info.Value != "mock_PATH" {
		t.Errorf("expected value=mock_PATH, got %s", info.Value)
	}
}

func TestSetVar(t *testing.T) {
	client, cleanup := setupTestClient(t)
	defer cleanup()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	client.Auth(ctx, token)

	if err := client.State.SetVar(ctx, "FOO", "bar", nil); err != nil {
		t.Fatal(err)
	}
}

func TestUnsetVar(t *testing.T) {
	client, cleanup := setupTestClient(t)
	defer cleanup()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	client.Auth(ctx, token)

	if err := client.State.UnsetVar(ctx, "FOO"); err != nil {
		t.Fatal(err)
	}
}

func TestGetFunc(t *testing.T) {
	client, cleanup := setupTestClient(t)
	defer cleanup()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	client.Auth(ctx, token)

	info, err := client.State.GetFunc(ctx, "myfunc")
	if err != nil {
		t.Fatal(err)
	}
	if info.Name != "myfunc" {
		t.Errorf("expected name=myfunc, got %s", info.Name)
	}
}

func TestGetAlias(t *testing.T) {
	client, cleanup := setupTestClient(t)
	defer cleanup()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	client.Auth(ctx, token)

	info, err := client.State.GetAlias(ctx, "ll")
	if err != nil {
		t.Fatal(err)
	}
	if info.Name != "ll" {
		t.Errorf("expected name=ll, got %s", info.Name)
	}
}

func TestInspect(t *testing.T) {
	client, cleanup := setupTestClient(t)
	defer cleanup()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	client.Auth(ctx, token)

	items, err := client.State.Inspect(ctx, "vars")
	if err != nil {
		t.Fatal(err)
	}
	if len(items) == 0 {
		t.Error("expected at least one inspect item")
	}
}
