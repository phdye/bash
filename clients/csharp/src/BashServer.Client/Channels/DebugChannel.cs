using System.Text.Json;
using Msg = System.Collections.Generic.Dictionary<string, System.Text.Json.JsonElement>;

namespace BashServer.Client.Channels;

/// <summary>Channel 4: debugging (breakpoints, stepping, AST).</summary>
public class DebugChannel
{
    private readonly Func<Dictionary<string, object?>, Task> _send;
    private readonly Func<int, CancellationToken, Task<Msg>> _recv;

    /// <summary>Fires when a breakpoint is hit.</summary>
    public event EventHandler<BreakHitEvent>? BreakHit;

    internal DebugChannel(
        Func<Dictionary<string, object?>, Task> send,
        Func<int, CancellationToken, Task<Msg>> recv)
    {
        _send = send;
        _recv = recv;
    }

    private async Task<Msg> RequestAsync(Dictionary<string, object?> msg, CancellationToken ct)
    {
        await _send(msg).ConfigureAwait(false);
        var resp = await _recv(Ch.Debug, ct).ConfigureAwait(false);
        if (NdjsonProtocol.GetType(resp) == "error")
            throw new ServerException(
                NdjsonProtocol.GetString(resp, "message", "debug error"), Ch.Debug);
        return resp;
    }

    public async Task EnableAsync(CancellationToken ct = default)
        => await RequestAsync(NdjsonProtocol.MakeMsg(Ch.Debug, "enable"), ct).ConfigureAwait(false);

    public async Task DisableAsync(CancellationToken ct = default)
        => await RequestAsync(NdjsonProtocol.MakeMsg(Ch.Debug, "disable"), ct).ConfigureAwait(false);

    public async Task<DebugStatus> StatusAsync(CancellationToken ct = default)
    {
        var resp = await RequestAsync(NdjsonProtocol.MakeMsg(Ch.Debug, "status"), ct).ConfigureAwait(false);
        return new DebugStatus(
            NdjsonProtocol.GetBool(resp, "active"),
            NdjsonProtocol.GetString(resp, "mode", "run"),
            NdjsonProtocol.GetInt(resp, "breakpoints"),
            NdjsonProtocol.GetInt(resp, "depth"));
    }

    public async Task<int> AddBreakpointAsync(string kind,
        string? pattern = null, int? line = null, string? condition = null,
        CancellationToken ct = default)
    {
        var extra = new Dictionary<string, object?> { ["kind"] = kind };
        if (pattern != null) extra["pattern"] = pattern;
        if (line != null) extra["line"] = line;
        if (condition != null) extra["condition"] = condition;
        var resp = await RequestAsync(NdjsonProtocol.MakeMsg(Ch.Debug, "break", extra), ct).ConfigureAwait(false);
        return NdjsonProtocol.GetInt(resp, "id", -1);
    }

    public async Task RemoveBreakpointAsync(int id, CancellationToken ct = default)
        => await RequestAsync(NdjsonProtocol.MakeMsg(Ch.Debug, "delete",
            new() { ["id"] = id }), ct).ConfigureAwait(false);

    public async Task EnableBreakpointAsync(int id, CancellationToken ct = default)
        => await RequestAsync(NdjsonProtocol.MakeMsg(Ch.Debug, "enable_bp",
            new() { ["id"] = id }), ct).ConfigureAwait(false);

    public async Task DisableBreakpointAsync(int id, CancellationToken ct = default)
        => await RequestAsync(NdjsonProtocol.MakeMsg(Ch.Debug, "disable_bp",
            new() { ["id"] = id }), ct).ConfigureAwait(false);

    public async Task<List<Breakpoint>> ListBreakpointsAsync(CancellationToken ct = default)
    {
        var resp = await RequestAsync(NdjsonProtocol.MakeMsg(Ch.Debug, "list"), ct).ConfigureAwait(false);
        var items = NdjsonProtocol.GetObjectArray(resp, "data");
        var result = new List<Breakpoint>();
        foreach (var bp in items)
        {
            result.Add(new Breakpoint(
                Id: NdjsonProtocol.GetInt(bp, "id"),
                Kind: NdjsonProtocol.GetString(bp, "type"),
                Enabled: NdjsonProtocol.GetBool(bp, "enabled", true),
                HitCount: NdjsonProtocol.GetInt(bp, "hit_count"),
                Pattern: bp.ContainsKey("pattern") ? NdjsonProtocol.GetString(bp, "pattern") : null,
                Line: bp.ContainsKey("line") ? NdjsonProtocol.GetInt(bp, "line") : null,
                Condition: bp.ContainsKey("condition") ? NdjsonProtocol.GetString(bp, "condition") : null));
        }
        return result;
    }

    public async Task ContinueAsync(CancellationToken ct = default)
        => await RequestAsync(NdjsonProtocol.MakeMsg(Ch.Debug, "continue"), ct).ConfigureAwait(false);

    public async Task StepAsync(CancellationToken ct = default)
        => await RequestAsync(NdjsonProtocol.MakeMsg(Ch.Debug, "step"), ct).ConfigureAwait(false);

    public async Task NextAsync(CancellationToken ct = default)
        => await RequestAsync(NdjsonProtocol.MakeMsg(Ch.Debug, "next"), ct).ConfigureAwait(false);

    public async Task FinishAsync(CancellationToken ct = default)
        => await RequestAsync(NdjsonProtocol.MakeMsg(Ch.Debug, "finish"), ct).ConfigureAwait(false);

    public async Task SkipAsync(CancellationToken ct = default)
        => await RequestAsync(NdjsonProtocol.MakeMsg(Ch.Debug, "skip"), ct).ConfigureAwait(false);

    public async Task<Msg> InspectAstAsync(CancellationToken ct = default)
    {
        var resp = await RequestAsync(NdjsonProtocol.MakeMsg(Ch.Debug, "inspect_ast"), ct).ConfigureAwait(false);
        return NdjsonProtocol.GetObject(resp, "data") ?? new Msg();
    }

    internal void Dispatch(Msg msg)
    {
        if (NdjsonProtocol.GetType(msg) == "break_hit")
        {
            BreakHit?.Invoke(this, new BreakHitEvent(
                Line: NdjsonProtocol.GetInt(msg, "line"),
                Command: NdjsonProtocol.GetString(msg, "command"),
                Depth: NdjsonProtocol.GetInt(msg, "depth")));
        }
    }
}
