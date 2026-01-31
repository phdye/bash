using System.Text.Json;
using Msg = System.Collections.Generic.Dictionary<string, System.Text.Json.JsonElement>;

namespace BashServer.Client.Channels;

/// <summary>Channel 3: command observation events.</summary>
public class ObserveChannel
{
    private readonly Func<Dictionary<string, object?>, Task> _send;
    private readonly Func<int, CancellationToken, Task<Msg>> _recv;

    /// <summary>Fires before a command executes.</summary>
    public event EventHandler<PreCommandEvent>? PreCommand;

    /// <summary>Fires after a command executes.</summary>
    public event EventHandler<PostCommandEvent>? PostCommand;

    internal ObserveChannel(
        Func<Dictionary<string, object?>, Task> send,
        Func<int, CancellationToken, Task<Msg>> recv)
    {
        _send = send;
        _recv = recv;
    }

    public async Task SubscribeAsync(int level = 1, CancellationToken ct = default)
    {
        await _send(NdjsonProtocol.MakeMsg(Ch.Observe, "subscribe",
            new() { ["level"] = level }));
        var resp = await _recv(Ch.Observe, ct);
        if (NdjsonProtocol.GetType(resp) == "error")
            throw new ServerException(
                NdjsonProtocol.GetString(resp, "message", "subscribe error"), Ch.Observe);
    }

    public async Task UnsubscribeAsync(CancellationToken ct = default)
    {
        await _send(NdjsonProtocol.MakeMsg(Ch.Observe, "unsubscribe"));
        var resp = await _recv(Ch.Observe, ct);
        if (NdjsonProtocol.GetType(resp) == "error")
            throw new ServerException(
                NdjsonProtocol.GetString(resp, "message", "unsubscribe error"), Ch.Observe);
    }

    internal void Dispatch(Msg msg)
    {
        var t = NdjsonProtocol.GetType(msg);
        var data = NdjsonProtocol.GetObject(msg, "data") ?? msg;

        switch (t)
        {
            case "pre_command":
                PreCommand?.Invoke(this, new PreCommandEvent(
                    Seq: NdjsonProtocol.GetInt(msg, "seq"),
                    Timestamp: NdjsonProtocol.GetLong(msg, "timestamp"),
                    Command: NdjsonProtocol.GetString(data, "command"),
                    Cwd: NdjsonProtocol.GetString(data, "cwd"),
                    LineNumber: NdjsonProtocol.GetInt(data, "line_number"),
                    IsSubshell: NdjsonProtocol.GetBool(data, "is_subshell"),
                    IsAsync: NdjsonProtocol.GetBool(data, "is_async")));
                break;
            case "post_command":
                PostCommand?.Invoke(this, new PostCommandEvent(
                    Seq: NdjsonProtocol.GetInt(msg, "seq"),
                    Timestamp: NdjsonProtocol.GetLong(msg, "timestamp"),
                    Command: NdjsonProtocol.GetString(data, "command"),
                    ExitStatus: NdjsonProtocol.GetInt(data, "exit_status"),
                    SignalNumber: NdjsonProtocol.GetInt(data, "signal_number"),
                    DurationMs: NdjsonProtocol.GetInt(data, "duration_ms")));
                break;
        }
    }
}
