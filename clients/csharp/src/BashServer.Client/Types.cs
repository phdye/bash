using System.Text.Json.Serialization;

namespace BashServer.Client;

/// <summary>Channel IDs.</summary>
public static class Ch
{
    public const int Control = 0;
    public const int Command = 1;
    public const int State = 2;
    public const int Observe = 3;
    public const int Debug = 4;
    public const int Pty = 5;
    public const int Max = 5;
}

/// <summary>Protocol limits and constants.</summary>
public static class Protocol
{
    public const int FrameMaxPayload = 1_048_576;
    public const int TokenHexLen = 64;
    public const int ObserveLevelOff = 0;
    public const int ObserveLevelCommand = 1;
}

/// <summary>Result of a command evaluation.</summary>
public record EvalResult(
    [property: JsonPropertyName("stdout")] string Stdout,
    [property: JsonPropertyName("stderr")] string Stderr,
    [property: JsonPropertyName("exit_code")] int ExitCode
);

/// <summary>Variable information.</summary>
public record VarInfo(
    [property: JsonPropertyName("name")] string Name,
    [property: JsonPropertyName("value")] string Value,
    [property: JsonPropertyName("attributes")] List<string> Attributes
);

/// <summary>Function information.</summary>
public record FuncInfo(
    [property: JsonPropertyName("name")] string Name,
    [property: JsonPropertyName("definition")] string Definition
);

/// <summary>Alias information.</summary>
public record AliasInfo(
    [property: JsonPropertyName("name")] string Name,
    [property: JsonPropertyName("value")] string Value
);

/// <summary>Trap information.</summary>
public record TrapInfo(
    [property: JsonPropertyName("signal")] string Signal,
    [property: JsonPropertyName("command")] string Command
);

/// <summary>Pre-command observation event.</summary>
public record PreCommandEvent(
    [property: JsonPropertyName("seq")] int Seq,
    [property: JsonPropertyName("timestamp")] long Timestamp,
    [property: JsonPropertyName("command")] string Command,
    [property: JsonPropertyName("cwd")] string Cwd,
    [property: JsonPropertyName("line_number")] int LineNumber = 0,
    [property: JsonPropertyName("is_subshell")] bool IsSubshell = false,
    [property: JsonPropertyName("is_async")] bool IsAsync = false
);

/// <summary>Post-command observation event.</summary>
public record PostCommandEvent(
    [property: JsonPropertyName("seq")] int Seq,
    [property: JsonPropertyName("timestamp")] long Timestamp,
    [property: JsonPropertyName("command")] string Command,
    [property: JsonPropertyName("exit_status")] int ExitStatus,
    [property: JsonPropertyName("signal_number")] int SignalNumber = 0,
    [property: JsonPropertyName("duration_ms")] int DurationMs = 0
);

/// <summary>Debug breakpoint.</summary>
public record Breakpoint(
    [property: JsonPropertyName("id")] int Id,
    [property: JsonPropertyName("type")] string Kind,
    [property: JsonPropertyName("enabled")] bool Enabled = true,
    [property: JsonPropertyName("hit_count")] int HitCount = 0,
    [property: JsonPropertyName("pattern")] string? Pattern = null,
    [property: JsonPropertyName("line")] int? Line = null,
    [property: JsonPropertyName("condition")] string? Condition = null
);

/// <summary>Debugger break hit event.</summary>
public record BreakHitEvent(
    [property: JsonPropertyName("line")] int Line,
    [property: JsonPropertyName("command")] string Command,
    [property: JsonPropertyName("depth")] int Depth = 0
);

/// <summary>Debugger status.</summary>
public record DebugStatus(
    [property: JsonPropertyName("active")] bool Active,
    [property: JsonPropertyName("mode")] string Mode,
    [property: JsonPropertyName("breakpoints")] int Breakpoints,
    [property: JsonPropertyName("depth")] int Depth = 0
);

/// <summary>PTY spawn options.</summary>
public record PtyOpts
{
    public int Rows { get; init; } = 24;
    public int Cols { get; init; } = 80;
    public string? Shell { get; init; }
    public bool StripAnsi { get; init; }
}

/// <summary>PTY session info.</summary>
public record PtyInfo(
    [property: JsonPropertyName("rows")] int Rows,
    [property: JsonPropertyName("cols")] int Cols,
    [property: JsonPropertyName("pid")] int Pid,
    [property: JsonPropertyName("strip_ansi")] bool StripAnsi = false
);
