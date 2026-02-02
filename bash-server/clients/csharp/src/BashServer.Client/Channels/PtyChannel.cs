using System.Text.Json;
using Msg = System.Collections.Generic.Dictionary<string, System.Text.Json.JsonElement>;

namespace BashServer.Client.Channels;

/// <summary>Channel 5: pseudo-terminal I/O.</summary>
public class PtyChannel
{
    private readonly Func<Dictionary<string, object?>, Task> _send;
    private readonly Func<int, CancellationToken, Task<Msg>> _recv;

    /// <summary>Fires when PTY output is received.</summary>
    public event EventHandler<string>? Output;

    /// <summary>Fires when the PTY process exits.</summary>
    public event EventHandler<int>? Exit;

    internal PtyChannel(
        Func<Dictionary<string, object?>, Task> send,
        Func<int, CancellationToken, Task<Msg>> recv)
    {
        _send = send;
        _recv = recv;
    }

    public async Task<PtyInfo> SpawnAsync(PtyOpts? opts = null, CancellationToken ct = default)
    {
        int rows = opts?.Rows ?? 24, cols = opts?.Cols ?? 80;
        var extra = new Dictionary<string, object?> { ["rows"] = rows, ["cols"] = cols };
        if (opts?.Shell != null) extra["shell"] = opts.Shell;
        if (opts?.StripAnsi == true) extra["strip_ansi"] = true;

        await _send(NdjsonProtocol.MakeMsg(Ch.Pty, "spawn", extra)).ConfigureAwait(false);
        var resp = await _recv(Ch.Pty, ct).ConfigureAwait(false);
        if (NdjsonProtocol.GetType(resp) == "error")
            throw new ServerException(
                NdjsonProtocol.GetString(resp, "message", "pty spawn error"), Ch.Pty);

        return new PtyInfo(
            NdjsonProtocol.GetInt(resp, "rows", rows),
            NdjsonProtocol.GetInt(resp, "cols", cols),
            NdjsonProtocol.GetInt(resp, "pid"),
            NdjsonProtocol.GetBool(resp, "strip_ansi"));
    }

    public async Task WriteInputAsync(string data, CancellationToken ct = default)
    {
        var encoded = Convert.ToBase64String(System.Text.Encoding.UTF8.GetBytes(data));
        await _send(NdjsonProtocol.MakeMsg(Ch.Pty, "input",
            new() { ["data"] = encoded, ["encoding"] = "base64" })).ConfigureAwait(false);
    }

    public async Task ResizeAsync(int rows, int cols, CancellationToken ct = default)
    {
        await _send(NdjsonProtocol.MakeMsg(Ch.Pty, "resize",
            new() { ["rows"] = rows, ["cols"] = cols })).ConfigureAwait(false);
        var resp = await _recv(Ch.Pty, ct).ConfigureAwait(false);
        if (NdjsonProtocol.GetType(resp) == "error")
            throw new ServerException(
                NdjsonProtocol.GetString(resp, "message", "resize error"), Ch.Pty);
    }

    public async Task SignalAsync(string name, CancellationToken ct = default)
    {
        await _send(NdjsonProtocol.MakeMsg(Ch.Pty, "signal",
            new() { ["signal"] = name })).ConfigureAwait(false);
        var resp = await _recv(Ch.Pty, ct).ConfigureAwait(false);
        if (NdjsonProtocol.GetType(resp) == "error")
            throw new ServerException(
                NdjsonProtocol.GetString(resp, "message", "signal error"), Ch.Pty);
    }

    public async Task CloseAsync(CancellationToken ct = default)
    {
        await _send(NdjsonProtocol.MakeMsg(Ch.Pty, "close")).ConfigureAwait(false);
    }

    internal void Dispatch(Msg msg)
    {
        var t = NdjsonProtocol.GetType(msg);
        switch (t)
        {
            case "output":
                var data = NdjsonProtocol.GetString(msg, "data");
                if (NdjsonProtocol.GetString(msg, "encoding") == "base64")
                {
                    try
                    {
                        data = System.Text.Encoding.UTF8.GetString(
                            Convert.FromBase64String(data));
                    }
                    catch (FormatException) { /* Malformed base64; use raw data. */ }
                }
                Output?.Invoke(this, data);
                break;
            case "exit":
                Exit?.Invoke(this, NdjsonProtocol.GetInt(msg, "exit_code", -1));
                break;
        }
    }
}
