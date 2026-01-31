// Example: set breakpoints, step through commands, inspect AST.
package main

import (
	"context"
	"fmt"
	"log"
	"os"

	"github.com/cygwin/bash-server/clients/go/bashclient"
)

func main() {
	socketPath := "/tmp/bash-server/sock"
	token := "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	if len(os.Args) > 1 {
		socketPath = os.Args[1]
	}
	if len(os.Args) > 2 {
		token = os.Args[2]
	}

	ctx := context.Background()
	client, err := bashclient.Connect(ctx, socketPath)
	if err != nil {
		log.Fatal(err)
	}
	defer client.Close()

	if err := client.Auth(ctx, token); err != nil {
		log.Fatal(err)
	}

	if err := client.Debug.Enable(ctx); err != nil {
		log.Fatal(err)
	}

	bpID, err := client.Debug.AddBreakpoint(ctx, "command", &bashclient.BreakpointOpts{
		Pattern: "echo",
	})
	if err != nil {
		log.Fatal(err)
	}
	fmt.Printf("Breakpoint set: id=%d\n", bpID)

	bps, err := client.Debug.ListBreakpoints(ctx)
	if err != nil {
		log.Fatal(err)
	}
	for _, bp := range bps {
		fmt.Printf("  BP#%d: kind=%s enabled=%t\n", bp.ID, bp.Kind, bp.Enabled)
	}

	client.Debug.OnBreakHit(func(e bashclient.BreakHitEvent) {
		fmt.Printf("[BREAK] line=%d cmd=%q\n", e.Line, e.Command)
		ast, err := client.Debug.InspectAST(ctx)
		if err == nil {
			fmt.Printf("  AST: %v\n", ast)
		}
		client.Debug.Continue(ctx)
	})

	result, err := client.Eval(ctx, "echo breakpoint_test")
	if err != nil {
		log.Fatal(err)
	}
	fmt.Printf("Result: %s", result.Stdout)

	client.Debug.RemoveBreakpoint(ctx, bpID)
	client.Debug.Disable(ctx)
}
