// Package bashclient provides a Go client for the bash-server v2 NDJSON protocol.
package bashclient

// Channel IDs.
const (
	ChanControl = 0
	ChanCommand = 1
	ChanState   = 2
	ChanObserve = 3
	ChanDebug   = 4
	ChanPty     = 5
	ChanMax     = 5
)

// Protocol limits.
const (
	FrameMaxPayload     = 1048576
	TokenHexLen         = 64
	ObserveLevelOff     = 0
	ObserveLevelCommand = 1
)

// Message is a raw protocol message.
type Message map[string]any

// EvalResult holds the result of a command evaluation.
type EvalResult struct {
	Stdout   string `json:"stdout"`
	Stderr   string `json:"stderr"`
	ExitCode int    `json:"exit_code"`
}

// VarInfo holds variable information.
type VarInfo struct {
	Name       string   `json:"name"`
	Value      string   `json:"value"`
	Attributes []string `json:"attributes"`
}

// FuncInfo holds function information.
type FuncInfo struct {
	Name       string `json:"name"`
	Definition string `json:"definition"`
}

// AliasInfo holds alias information.
type AliasInfo struct {
	Name  string `json:"name"`
	Value string `json:"value"`
}

// TrapInfo holds trap information.
type TrapInfo struct {
	Signal  string `json:"signal"`
	Command string `json:"command"`
}

// PreCommandEvent is an observation event before command execution.
type PreCommandEvent struct {
	Seq        int    `json:"seq"`
	Timestamp  int64  `json:"timestamp"`
	Command    string `json:"command"`
	Cwd        string `json:"cwd"`
	LineNumber int    `json:"line_number"`
	IsSubshell bool   `json:"is_subshell"`
	IsAsync    bool   `json:"is_async"`
}

// PostCommandEvent is an observation event after command execution.
type PostCommandEvent struct {
	Seq          int    `json:"seq"`
	Timestamp    int64  `json:"timestamp"`
	Command      string `json:"command"`
	ExitStatus   int    `json:"exit_status"`
	SignalNumber int    `json:"signal_number"`
	DurationMs   int    `json:"duration_ms"`
}

// Breakpoint describes a debug breakpoint.
type Breakpoint struct {
	ID        int    `json:"id"`
	Kind      string `json:"type"`
	Enabled   bool   `json:"enabled"`
	HitCount  int    `json:"hit_count"`
	Pattern   string `json:"pattern,omitempty"`
	Line      int    `json:"line,omitempty"`
	Condition string `json:"condition,omitempty"`
}

// BreakpointOpts holds optional parameters for AddBreakpoint.
type BreakpointOpts struct {
	Pattern   string
	Line      int
	Condition string
}

// BreakHitEvent is a debugger break hit event.
type BreakHitEvent struct {
	Line    int    `json:"line"`
	Command string `json:"command"`
	Depth   int    `json:"depth"`
}

// DebugStatus holds debugger status.
type DebugStatus struct {
	Active      bool   `json:"active"`
	Mode        string `json:"mode"`
	Breakpoints int    `json:"breakpoints"`
	Depth       int    `json:"depth"`
}

// PtyOpts holds optional parameters for Spawn.
type PtyOpts struct {
	Rows      int
	Cols      int
	Shell     string
	StripAnsi bool
}

// PtyInfo holds PTY session info.
type PtyInfo struct {
	Rows      int  `json:"rows"`
	Cols      int  `json:"cols"`
	Pid       int  `json:"pid"`
	StripAnsi bool `json:"strip_ansi"`
}

// InspectItem represents a single item from a state inspect response.
type InspectItem struct {
	Name       string   `json:"name,omitempty"`
	Value      string   `json:"value,omitempty"`
	Definition string   `json:"definition,omitempty"`
	Attributes []string `json:"attributes,omitempty"`
	Signal     string   `json:"signal,omitempty"`
	Command    string   `json:"command,omitempty"`
}
