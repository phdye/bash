// Example: spawn a PTY session and relay I/O.
package main

import (
	"context"
	"fmt"
	"log"
	"os"
	"sync"
	"time"

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

	var wg sync.WaitGroup
	wg.Add(1)

	client.Pty.OnOutput(func(data string) {
		fmt.Print(data)
	})
	client.Pty.OnExit(func(code int) {
		fmt.Printf("\n[PTY exited with code %d]\n", code)
		wg.Done()
	})

	info, err := client.Pty.Spawn(ctx, &bashclient.PtyOpts{
		Rows: 24, Cols: 80, StripAnsi: true,
	})
	if err != nil {
		log.Fatal(err)
	}
	fmt.Printf("PTY spawned: pid=%d %dx%d\n", info.Pid, info.Rows, info.Cols)

	client.Pty.WriteInput(ctx, "echo 'Hello from PTY!'\n")
	time.Sleep(500 * time.Millisecond)
	client.Pty.WriteInput(ctx, "exit\n")

	done := make(chan struct{})
	go func() {
		wg.Wait()
		close(done)
	}()

	select {
	case <-done:
	case <-time.After(5 * time.Second):
		fmt.Println("[Timeout waiting for PTY exit]")
		client.Pty.Close(ctx)
	}
}
