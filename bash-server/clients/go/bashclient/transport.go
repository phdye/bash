package bashclient

import (
	"bufio"
	"context"
	"fmt"
	"io"
	"net"
	"os"
	"os/exec"
	"sync"
)

// Transport is the interface for reading/writing NDJSON lines.
type Transport interface {
	ReadLine() ([]byte, error)
	Write(data []byte) error
	Close() error
	IsOpen() bool
}

// UnixSocketTransport connects via Unix domain socket.
type UnixSocketTransport struct {
	conn   net.Conn
	reader *bufio.Reader
	mu     sync.Mutex
	open   bool
}

// ConnectUnixSocket creates a new Unix socket transport.
func ConnectUnixSocket(ctx context.Context, path string) (*UnixSocketTransport, error) {
	var d net.Dialer
	conn, err := d.DialContext(ctx, "unix", path)
	if err != nil {
		return nil, newTransportErrorf(err, "cannot connect to %s: %v", path, err)
	}
	return &UnixSocketTransport{
		conn:   conn,
		reader: bufio.NewReaderSize(conn, 64*1024),
		open:   true,
	}, nil
}

// ReadLine reads one NDJSON line from the socket.
func (t *UnixSocketTransport) ReadLine() ([]byte, error) {
	line, err := t.reader.ReadBytes('\n')
	if err != nil {
		t.mu.Lock()
		t.open = false
		t.mu.Unlock()
		if err == io.EOF {
			return nil, newTransportError("connection closed")
		}
		return nil, newTransportErrorf(err, "read error: %v", err)
	}
	return line, nil
}

// Write sends data to the socket.
func (t *UnixSocketTransport) Write(data []byte) error {
	t.mu.Lock()
	defer t.mu.Unlock()
	if !t.open {
		return newTransportError("not connected")
	}
	_, err := t.conn.Write(data)
	if err != nil {
		t.open = false
		return newTransportErrorf(err, "write error: %v", err)
	}
	return nil
}

// Close closes the socket connection.
func (t *UnixSocketTransport) Close() error {
	t.mu.Lock()
	defer t.mu.Unlock()
	t.open = false
	if t.conn != nil {
		return t.conn.Close()
	}
	return nil
}

// IsOpen returns true if the socket is open.
func (t *UnixSocketTransport) IsOpen() bool {
	t.mu.Lock()
	defer t.mu.Unlock()
	return t.open
}

// StdioTransport connects via subprocess stdin/stdout.
type StdioTransport struct {
	cmd    *exec.Cmd
	reader *bufio.Reader
	writer io.WriteCloser
	mu     sync.Mutex
	open   bool
}

// ConnectStdioTransport starts a subprocess and connects to its stdio.
func ConnectStdioTransport(ctx context.Context, args ...string) (*StdioTransport, error) {
	if len(args) == 0 {
		return nil, newTransportError("no command specified")
	}
	cmd := exec.CommandContext(ctx, args[0], args[1:]...)
	stdin, err := cmd.StdinPipe()
	if err != nil {
		return nil, newTransportErrorf(err, "cannot create stdin pipe: %v", err)
	}
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return nil, newTransportErrorf(err, "cannot create stdout pipe: %v", err)
	}
	cmd.Stderr = os.Stderr
	if err := cmd.Start(); err != nil {
		return nil, newTransportErrorf(err, "cannot start process: %v", err)
	}
	return &StdioTransport{
		cmd:    cmd,
		reader: bufio.NewReaderSize(stdout, 64*1024),
		writer: stdin,
		open:   true,
	}, nil
}

// ReadLine reads one NDJSON line from the subprocess stdout.
func (t *StdioTransport) ReadLine() ([]byte, error) {
	line, err := t.reader.ReadBytes('\n')
	if err != nil {
		t.mu.Lock()
		t.open = false
		t.mu.Unlock()
		if err == io.EOF {
			return nil, newTransportError("connection closed")
		}
		return nil, newTransportErrorf(err, "read error: %v", err)
	}
	return line, nil
}

// Write sends data to the subprocess stdin.
func (t *StdioTransport) Write(data []byte) error {
	t.mu.Lock()
	defer t.mu.Unlock()
	if !t.open {
		return newTransportError("not connected")
	}
	_, err := t.writer.Write(data)
	if err != nil {
		t.open = false
		return newTransportErrorf(err, "write error: %v", err)
	}
	return nil
}

// Close kills the subprocess and releases resources.
func (t *StdioTransport) Close() error {
	t.mu.Lock()
	defer t.mu.Unlock()
	t.open = false
	if t.writer != nil {
		t.writer.Close()
	}
	if t.cmd != nil && t.cmd.Process != nil {
		t.cmd.Process.Kill()
		t.cmd.Wait()
	}
	return nil
}

// IsOpen returns true if the subprocess is still running.
func (t *StdioTransport) IsOpen() bool {
	t.mu.Lock()
	defer t.mu.Unlock()
	return t.open
}

// FdTransport connects via an inherited file descriptor.
type FdTransport struct {
	file   *os.File
	reader *bufio.Reader
	mu     sync.Mutex
	open   bool
}

// ConnectFdTransport connects via an existing file descriptor.
func ConnectFdTransport(_ context.Context, fd int) (*FdTransport, error) {
	file := os.NewFile(uintptr(fd), fmt.Sprintf("fd:%d", fd))
	if file == nil {
		return nil, newTransportError(fmt.Sprintf("cannot open fd %d", fd))
	}
	return &FdTransport{
		file:   file,
		reader: bufio.NewReaderSize(file, 64*1024),
		open:   true,
	}, nil
}

// ReadLine reads one NDJSON line from the file descriptor.
func (t *FdTransport) ReadLine() ([]byte, error) {
	line, err := t.reader.ReadBytes('\n')
	if err != nil {
		t.mu.Lock()
		t.open = false
		t.mu.Unlock()
		if err == io.EOF {
			return nil, newTransportError("connection closed")
		}
		return nil, newTransportErrorf(err, "read error: %v", err)
	}
	return line, nil
}

// Write sends data to the file descriptor.
func (t *FdTransport) Write(data []byte) error {
	t.mu.Lock()
	defer t.mu.Unlock()
	if !t.open {
		return newTransportError("not connected")
	}
	_, err := t.file.Write(data)
	if err != nil {
		t.open = false
		return newTransportErrorf(err, "write error: %v", err)
	}
	return nil
}

// Close closes the file descriptor.
func (t *FdTransport) Close() error {
	t.mu.Lock()
	defer t.mu.Unlock()
	t.open = false
	if t.file != nil {
		return t.file.Close()
	}
	return nil
}

// IsOpen returns true if the file descriptor is open.
func (t *FdTransport) IsOpen() bool {
	t.mu.Lock()
	defer t.mu.Unlock()
	return t.open
}

// NamedPipeTransport connects via a Windows Named Pipe.
type NamedPipeTransport struct {
	conn   net.Conn
	reader *bufio.Reader
	mu     sync.Mutex
	open   bool
}

// ConnectNamedPipeTransport connects to a Named Pipe.
// On Cygwin, pipes are accessible as Unix domain sockets.
func ConnectNamedPipeTransport(ctx context.Context, pipeName string) (*NamedPipeTransport, error) {
	var d net.Dialer
	conn, err := d.DialContext(ctx, "unix", pipeName)
	if err != nil {
		return nil, newTransportErrorf(err, "cannot connect to pipe %s: %v", pipeName, err)
	}
	return &NamedPipeTransport{
		conn:   conn,
		reader: bufio.NewReaderSize(conn, 64*1024),
		open:   true,
	}, nil
}

// ReadLine reads one NDJSON line from the pipe.
func (t *NamedPipeTransport) ReadLine() ([]byte, error) {
	line, err := t.reader.ReadBytes('\n')
	if err != nil {
		t.mu.Lock()
		t.open = false
		t.mu.Unlock()
		if err == io.EOF {
			return nil, newTransportError("connection closed")
		}
		return nil, newTransportErrorf(err, "read error: %v", err)
	}
	return line, nil
}

// Write sends data to the pipe.
func (t *NamedPipeTransport) Write(data []byte) error {
	t.mu.Lock()
	defer t.mu.Unlock()
	if !t.open {
		return newTransportError("not connected")
	}
	_, err := t.conn.Write(data)
	if err != nil {
		t.open = false
		return newTransportErrorf(err, "write error: %v", err)
	}
	return nil
}

// Close closes the named pipe connection.
func (t *NamedPipeTransport) Close() error {
	t.mu.Lock()
	defer t.mu.Unlock()
	t.open = false
	if t.conn != nil {
		return t.conn.Close()
	}
	return nil
}

// IsOpen returns true if the pipe connection is open.
func (t *NamedPipeTransport) IsOpen() bool {
	t.mu.Lock()
	defer t.mu.Unlock()
	return t.open
}

// PipeTransport is an in-memory transport for testing, using io.Pipe pairs.
type PipeTransport struct {
	reader *bufio.Reader
	writer io.Writer
	rClose io.Closer
	wClose io.Closer
	mu     sync.Mutex
	open   bool
}

// NewPipeTransport creates a pair of connected in-memory transports.
func NewPipeTransport() (client *PipeTransport, server *PipeTransport) {
	cr, sw := io.Pipe() // server writes, client reads
	sr, cw := io.Pipe() // client writes, server reads
	client = &PipeTransport{
		reader: bufio.NewReaderSize(cr, 64*1024),
		writer: cw,
		rClose: cr,
		wClose: cw,
		open:   true,
	}
	server = &PipeTransport{
		reader: bufio.NewReaderSize(sr, 64*1024),
		writer: sw,
		rClose: sr,
		wClose: sw,
		open:   true,
	}
	return
}

// ReadLine reads one NDJSON line from the in-memory pipe.
func (t *PipeTransport) ReadLine() ([]byte, error) {
	line, err := t.reader.ReadBytes('\n')
	if err != nil {
		t.mu.Lock()
		t.open = false
		t.mu.Unlock()
		if err == io.EOF {
			return nil, newTransportError("connection closed")
		}
		return nil, newTransportErrorf(err, "read error: %v", err)
	}
	return line, nil
}

// Write sends data to the in-memory pipe.
func (t *PipeTransport) Write(data []byte) error {
	t.mu.Lock()
	defer t.mu.Unlock()
	if !t.open {
		return newTransportError("not connected")
	}
	_, err := t.writer.Write(data)
	if err != nil {
		t.open = false
		return newTransportErrorf(err, "write error: %v", err)
	}
	return nil
}

// Close closes both ends of the pipe.
func (t *PipeTransport) Close() error {
	t.mu.Lock()
	defer t.mu.Unlock()
	t.open = false
	if t.wClose != nil {
		t.wClose.Close()
	}
	if t.rClose != nil {
		t.rClose.Close()
	}
	return nil
}

// IsOpen returns true if the pipe is open.
func (t *PipeTransport) IsOpen() bool {
	t.mu.Lock()
	defer t.mu.Unlock()
	return t.open
}
