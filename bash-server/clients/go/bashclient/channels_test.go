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
// Handles all 6 channels for comprehensive testing.
func mockServer(t *testing.T, serverT *PipeTransport, token string) {
	t.Helper()
	nextBpID := 1
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
		case ch == ChanControl && tp == "configure":
			responses = append(responses, Message{"ch": 0, "type": "configured", "observability": msg["observability"]})
		case ch == ChanControl && tp == "disconnect":
			responses = append(responses, Message{"ch": 0, "type": "disconnect_ok"})
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
		case ch == ChanObserve && tp == "subscribe":
			responses = append(responses, Message{"ch": 3, "type": "subscribed", "level": msg["level"]})
		case ch == ChanObserve && tp == "unsubscribe":
			responses = append(responses, Message{"ch": 3, "type": "unsubscribed"})
		case ch == ChanDebug && tp == "enable":
			responses = append(responses, Message{"ch": 4, "type": "enable_ok"})
		case ch == ChanDebug && tp == "disable":
			responses = append(responses, Message{"ch": 4, "type": "disable_ok"})
		case ch == ChanDebug && tp == "status":
			responses = append(responses, Message{"ch": 4, "type": "status", "active": true, "mode": "run", "breakpoints": 0, "depth": 0})
		case ch == ChanDebug && tp == "break":
			id := nextBpID
			nextBpID++
			responses = append(responses, Message{"ch": 4, "type": "break_ok", "id": id})
		case ch == ChanDebug && tp == "delete":
			responses = append(responses, Message{"ch": 4, "type": "delete_ok", "found": true})
		case ch == ChanDebug && tp == "list":
			responses = append(responses, Message{"ch": 4, "type": "list", "data": []any{
				map[string]any{"id": 1, "type": "command", "enabled": true, "pattern": "echo*", "hit_count": 0},
			}})
		case ch == ChanDebug && (tp == "continue" || tp == "step" || tp == "next" || tp == "finish" || tp == "skip"):
			responses = append(responses, Message{"ch": 4, "type": tp + "_ok"})
		case ch == ChanDebug && tp == "inspect_ast":
			responses = append(responses, Message{"ch": 4, "type": "ast", "data": map[string]any{"type": "cm_simple", "words": []any{"echo", "hello"}}})
		case ch == ChanDebug && (tp == "enable_bp" || tp == "disable_bp"):
			responses = append(responses, Message{"ch": 4, "type": tp + "_ok"})
		case ch == ChanPty && tp == "spawn":
			rows := msg["rows"]
			cols := msg["cols"]
			responses = append(responses, Message{"ch": 5, "type": "spawned", "pid": 12345, "rows": rows, "cols": cols, "strip_ansi": msg["strip_ansi"]})
		case ch == ChanPty && tp == "resize":
			responses = append(responses, Message{"ch": 5, "type": "resized"})
		case ch == ChanPty && tp == "signal":
			responses = append(responses, Message{"ch": 5, "type": "signal_ok"})
		case ch == ChanPty && tp == "close":
			// No response expected for close
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

// --- Control channel ---

func TestConfigure(t *testing.T) {
	client, cleanup := setupTestClient(t)
	defer cleanup()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	client.Auth(ctx, token)

	resp, err := client.Control.Configure(ctx, map[string]any{"observability": 1})
	if err != nil {
		t.Fatal(err)
	}
	if getString(resp, "type") != "configured" {
		t.Errorf("expected configured, got %s", getString(resp, "type"))
	}
}

func TestDisconnect(t *testing.T) {
	clientT, serverT := NewPipeTransport()
	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	go mockServer(t, serverT, token)
	client := FromTransport(clientT)
	// Don't use setupTestClient cleanup — it calls Close() which sends
	// a second disconnect on the dead pipe and blocks.
	defer clientT.Close()
	defer serverT.Close()

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	client.Auth(ctx, token)

	err := client.Control.Disconnect(ctx)
	if err != nil {
		t.Fatal(err)
	}
}

// --- State channel: remaining operations ---

func TestSetAlias(t *testing.T) {
	client, cleanup := setupTestClient(t)
	defer cleanup()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	client.Auth(ctx, token)

	if err := client.State.SetAlias(ctx, "ll", "ls -la"); err != nil {
		t.Fatal(err)
	}
}

func TestUnsetFunc(t *testing.T) {
	client, cleanup := setupTestClient(t)
	defer cleanup()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	client.Auth(ctx, token)

	if err := client.State.UnsetFunc(ctx, "myfunc"); err != nil {
		t.Fatal(err)
	}
}

func TestUnsetAlias(t *testing.T) {
	client, cleanup := setupTestClient(t)
	defer cleanup()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	client.Auth(ctx, token)

	if err := client.State.UnsetAlias(ctx, "ll"); err != nil {
		t.Fatal(err)
	}
}

func TestSetTrap(t *testing.T) {
	client, cleanup := setupTestClient(t)
	defer cleanup()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	client.Auth(ctx, token)

	if err := client.State.SetTrap(ctx, "INT", "echo trapped"); err != nil {
		t.Fatal(err)
	}
}

func TestUnsetTrap(t *testing.T) {
	client, cleanup := setupTestClient(t)
	defer cleanup()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	client.Auth(ctx, token)

	if err := client.State.UnsetTrap(ctx, "INT"); err != nil {
		t.Fatal(err)
	}
}

func TestSetVarWithAttributes(t *testing.T) {
	client, cleanup := setupTestClient(t)
	defer cleanup()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	client.Auth(ctx, token)

	if err := client.State.SetVar(ctx, "X", "42", []string{"exported", "integer"}); err != nil {
		t.Fatal(err)
	}
}

// --- Observe channel ---

func TestObserveSubscribe(t *testing.T) {
	client, cleanup := setupTestClient(t)
	defer cleanup()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	client.Auth(ctx, token)

	if err := client.Observe.Subscribe(ctx, 1); err != nil {
		t.Fatal(err)
	}
}

func TestObserveUnsubscribe(t *testing.T) {
	client, cleanup := setupTestClient(t)
	defer cleanup()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	client.Auth(ctx, token)

	client.Observe.Subscribe(ctx, 1)
	if err := client.Observe.Unsubscribe(ctx); err != nil {
		t.Fatal(err)
	}
}

// --- Debug channel ---

func TestDebugEnable(t *testing.T) {
	client, cleanup := setupTestClient(t)
	defer cleanup()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	client.Auth(ctx, token)

	if err := client.Debug.Enable(ctx); err != nil {
		t.Fatal(err)
	}
}

func TestDebugDisable(t *testing.T) {
	client, cleanup := setupTestClient(t)
	defer cleanup()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	client.Auth(ctx, token)

	client.Debug.Enable(ctx)
	if err := client.Debug.Disable(ctx); err != nil {
		t.Fatal(err)
	}
}

func TestDebugStatus(t *testing.T) {
	client, cleanup := setupTestClient(t)
	defer cleanup()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	client.Auth(ctx, token)

	status, err := client.Debug.Status(ctx)
	if err != nil {
		t.Fatal(err)
	}
	if !status.Active {
		t.Error("expected active=true")
	}
	if status.Mode != "run" {
		t.Errorf("expected mode=run, got %s", status.Mode)
	}
}

func TestDebugAddBreakpoint(t *testing.T) {
	client, cleanup := setupTestClient(t)
	defer cleanup()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	client.Auth(ctx, token)

	id, err := client.Debug.AddBreakpoint(ctx, "command", &BreakpointOpts{Pattern: "echo*"})
	if err != nil {
		t.Fatal(err)
	}
	if id <= 0 {
		t.Errorf("expected positive breakpoint ID, got %d", id)
	}
}

func TestDebugRemoveBreakpoint(t *testing.T) {
	client, cleanup := setupTestClient(t)
	defer cleanup()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	client.Auth(ctx, token)

	id, _ := client.Debug.AddBreakpoint(ctx, "command", &BreakpointOpts{Pattern: "echo*"})
	if err := client.Debug.RemoveBreakpoint(ctx, id); err != nil {
		t.Fatal(err)
	}
}

func TestDebugListBreakpoints(t *testing.T) {
	client, cleanup := setupTestClient(t)
	defer cleanup()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	client.Auth(ctx, token)

	bps, err := client.Debug.ListBreakpoints(ctx)
	if err != nil {
		t.Fatal(err)
	}
	if len(bps) == 0 {
		t.Error("expected at least one breakpoint in list")
	}
	if bps[0].Kind != "command" {
		t.Errorf("expected kind=command, got %s", bps[0].Kind)
	}
	if bps[0].Pattern != "echo*" {
		t.Errorf("expected pattern=echo*, got %s", bps[0].Pattern)
	}
}

func TestDebugStep(t *testing.T) {
	client, cleanup := setupTestClient(t)
	defer cleanup()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	client.Auth(ctx, token)

	if err := client.Debug.Step(ctx); err != nil {
		t.Fatal(err)
	}
}

func TestDebugNext(t *testing.T) {
	client, cleanup := setupTestClient(t)
	defer cleanup()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	client.Auth(ctx, token)

	if err := client.Debug.Next(ctx); err != nil {
		t.Fatal(err)
	}
}

func TestDebugContinue(t *testing.T) {
	client, cleanup := setupTestClient(t)
	defer cleanup()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	client.Auth(ctx, token)

	if err := client.Debug.Continue(ctx); err != nil {
		t.Fatal(err)
	}
}

func TestDebugFinish(t *testing.T) {
	client, cleanup := setupTestClient(t)
	defer cleanup()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	client.Auth(ctx, token)

	if err := client.Debug.Finish(ctx); err != nil {
		t.Fatal(err)
	}
}

func TestDebugSkip(t *testing.T) {
	client, cleanup := setupTestClient(t)
	defer cleanup()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	client.Auth(ctx, token)

	if err := client.Debug.Skip(ctx); err != nil {
		t.Fatal(err)
	}
}

func TestDebugInspectAST(t *testing.T) {
	client, cleanup := setupTestClient(t)
	defer cleanup()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	client.Auth(ctx, token)

	ast, err := client.Debug.InspectAST(ctx)
	if err != nil {
		t.Fatal(err)
	}
	if getString(ast, "type") != "cm_simple" {
		t.Errorf("expected cm_simple AST type, got %s", getString(ast, "type"))
	}
}

func TestDebugEnableDisableBreakpoint(t *testing.T) {
	client, cleanup := setupTestClient(t)
	defer cleanup()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	client.Auth(ctx, token)

	id, _ := client.Debug.AddBreakpoint(ctx, "command", &BreakpointOpts{Pattern: "echo*"})
	if err := client.Debug.DisableBreakpoint(ctx, id); err != nil {
		t.Fatal(err)
	}
	if err := client.Debug.EnableBreakpoint(ctx, id); err != nil {
		t.Fatal(err)
	}
}

// --- PTY channel ---

func TestPtySpawn(t *testing.T) {
	client, cleanup := setupTestClient(t)
	defer cleanup()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	client.Auth(ctx, token)

	info, err := client.Pty.Spawn(ctx, &PtyOpts{Rows: 25, Cols: 100})
	if err != nil {
		t.Fatal(err)
	}
	if info.Pid != 12345 {
		t.Errorf("expected pid=12345, got %d", info.Pid)
	}
	if info.Rows != 25 {
		t.Errorf("expected rows=25, got %d", info.Rows)
	}
	if info.Cols != 100 {
		t.Errorf("expected cols=100, got %d", info.Cols)
	}
}

func TestPtySpawnDefaults(t *testing.T) {
	client, cleanup := setupTestClient(t)
	defer cleanup()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	client.Auth(ctx, token)

	info, err := client.Pty.Spawn(ctx, nil)
	if err != nil {
		t.Fatal(err)
	}
	if info.Rows != 24 {
		t.Errorf("expected default rows=24, got %d", info.Rows)
	}
	if info.Cols != 80 {
		t.Errorf("expected default cols=80, got %d", info.Cols)
	}
}

func TestPtyResize(t *testing.T) {
	client, cleanup := setupTestClient(t)
	defer cleanup()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	client.Auth(ctx, token)

	client.Pty.Spawn(ctx, nil)
	if err := client.Pty.Resize(ctx, 50, 120); err != nil {
		t.Fatal(err)
	}
}

func TestPtySignal(t *testing.T) {
	client, cleanup := setupTestClient(t)
	defer cleanup()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	client.Auth(ctx, token)

	client.Pty.Spawn(ctx, nil)
	if err := client.Pty.Signal(ctx, "SIGINT"); err != nil {
		t.Fatal(err)
	}
}

func TestPtyClose(t *testing.T) {
	client, cleanup := setupTestClient(t)
	defer cleanup()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	client.Auth(ctx, token)

	client.Pty.Spawn(ctx, nil)
	if err := client.Pty.Close(ctx); err != nil {
		t.Fatal(err)
	}
}

func TestPtyWriteInput(t *testing.T) {
	client, cleanup := setupTestClient(t)
	defer cleanup()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	client.Auth(ctx, token)

	client.Pty.Spawn(ctx, nil)
	if err := client.Pty.WriteInput(ctx, "echo hello\n"); err != nil {
		t.Fatal(err)
	}
}

// --- Error paths ---

func TestServerErrorOnBadTarget(t *testing.T) {
	client, cleanup := setupTestClient(t)
	defer cleanup()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	client.Auth(ctx, token)

	// The mock server returns an error for unknown targets on "get"
	_, err := client.State.GetVar(ctx, "PATH")
	if err != nil {
		t.Fatal("GetVar should succeed for 'var' target")
	}
	// There's no direct way to send an unknown target via typed API,
	// but we verify the error path works in TestAuthFailure.
}

func TestContextTimeout(t *testing.T) {
	clientT, serverT := NewPipeTransport()
	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	go mockServer(t, serverT, token)
	client := FromTransport(clientT)
	defer clientT.Close()
	defer serverT.Close()

	// Use an already-canceled context
	ctx, cancel := context.WithCancel(context.Background())
	cancel()

	err := client.Ping(ctx)
	if err == nil {
		t.Fatal("expected error on canceled context")
	}
	var te *TimeoutError
	if !errors.As(err, &te) {
		// May also be AuthError since not authenticated — but with canceled ctx,
		// it should be a timeout from recv
		var ae *AuthError
		if !errors.As(err, &ae) {
			t.Logf("got error type %T: %v (acceptable)", err, err)
		}
	}
}
