using System.Text.Json;
using Msg = System.Collections.Generic.Dictionary<string, System.Text.Json.JsonElement>;

namespace BashServer.Client.Channels;

/// <summary>Channel 0: auth, ping, configure, disconnect.</summary>
public class ControlChannel
{
    private readonly Func<Dictionary<string, object?>, Task> _send;
    private readonly Func<int, CancellationToken, Task<Msg>> _recv;

    internal ControlChannel(
        Func<Dictionary<string, object?>, Task> send,
        Func<int, CancellationToken, Task<Msg>> recv)
    {
        _send = send;
        _recv = recv;
    }

    public async Task<Msg> AuthAsync(string token, CancellationToken ct = default)
    {
        await _send(NdjsonProtocol.MakeMsg(Ch.Control, "auth",
            new() { ["token"] = token })).ConfigureAwait(false);
        var resp = await _recv(Ch.Control, ct).ConfigureAwait(false);
        var t = NdjsonProtocol.GetType(resp);
        if (t == "error")
            throw new AuthException(NdjsonProtocol.GetString(resp, "message", "authentication failed"));
        if (t != "auth_ok")
            throw new AuthException($"unexpected auth response: {t}");
        return resp;
    }

    public async Task PingAsync(CancellationToken ct = default)
    {
        await _send(NdjsonProtocol.MakeMsg(Ch.Control, "ping")).ConfigureAwait(false);
        var resp = await _recv(Ch.Control, ct).ConfigureAwait(false);
        if (NdjsonProtocol.GetType(resp) != "pong")
            throw new ServerException($"expected pong, got {NdjsonProtocol.GetType(resp)}", Ch.Control);
    }

    public async Task<Msg> ConfigureAsync(object opts, CancellationToken ct = default)
    {
        var json = JsonSerializer.Serialize(opts);
        var extra = JsonSerializer.Deserialize<Dictionary<string, object?>>(json)
            ?? new Dictionary<string, object?>();
        await _send(NdjsonProtocol.MakeMsg(Ch.Control, "configure", extra)).ConfigureAwait(false);
        return await _recv(Ch.Control, ct).ConfigureAwait(false);
    }

    public async Task DisconnectAsync(CancellationToken ct = default)
    {
        await _send(NdjsonProtocol.MakeMsg(Ch.Control, "disconnect")).ConfigureAwait(false);
        try { await _recv(Ch.Control, ct).ConfigureAwait(false); }
        catch (OperationCanceledException) { }
        catch (TransportException) { }
    }
}
