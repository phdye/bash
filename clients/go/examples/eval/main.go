// Example: connect to bash-server, authenticate, and evaluate a command.
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

	result, err := client.Eval(ctx, "echo 'Hello from bash-server!'")
	if err != nil {
		log.Fatal(err)
	}
	fmt.Printf("stdout: %s", result.Stdout)
	fmt.Printf("stderr: %s", result.Stderr)
	fmt.Printf("exit_code: %d\n", result.ExitCode)
}
