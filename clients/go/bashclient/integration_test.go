//go:build integration

package bashclient

import (
	"context"
	"os/exec"
	"testing"
	"time"
)

func TestIntegrationStdioConnectAuthEval(t *testing.T) {
	if _, err := exec.LookPath("bash-server"); err != nil {
		t.Skip("bash-server not found in PATH")
	}

	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()

	token := "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
	client, err := ConnectStdio(ctx, "bash-server", "--stdio", "--token", token)
	if err != nil {
		t.Fatal(err)
	}
	defer client.Close()

	if err := client.Auth(ctx, token); err != nil {
		t.Fatal(err)
	}

	result, err := client.Eval(ctx, "echo integration_test")
	if err != nil {
		t.Fatal(err)
	}
	if result.ExitCode != 0 {
		t.Errorf("expected exit_code=0, got %d", result.ExitCode)
	}
	if result.Stdout == "" {
		t.Error("expected non-empty stdout")
	}
}
