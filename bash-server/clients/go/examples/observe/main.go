// Example: subscribe to command observation events.
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

	client.Observe.OnPreCommand(func(e bashclient.PreCommandEvent) {
		fmt.Printf("[PRE]  seq=%d cmd=%q cwd=%s\n", e.Seq, e.Command, e.Cwd)
	})
	client.Observe.OnPostCommand(func(e bashclient.PostCommandEvent) {
		fmt.Printf("[POST] seq=%d cmd=%q exit=%d dur=%dms\n",
			e.Seq, e.Command, e.ExitStatus, e.DurationMs)
	})

	if err := client.Observe.Subscribe(ctx, 1); err != nil {
		log.Fatal(err)
	}

	client.Eval(ctx, "echo one")
	client.Eval(ctx, "echo two")
	client.Eval(ctx, "false")

	client.Observe.Unsubscribe(ctx)
}
