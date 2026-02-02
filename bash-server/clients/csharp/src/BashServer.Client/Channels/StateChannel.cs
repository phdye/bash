using Msg = System.Collections.Generic.Dictionary<string, System.Text.Json.JsonElement>;

namespace BashServer.Client.Channels;

/// <summary>Channel 2: variable, function, alias, and trap management.</summary>
public class StateChannel
{
    private readonly Func<Dictionary<string, object?>, Task> _send;
    private readonly Func<int, CancellationToken, Task<Msg>> _recv;

    internal StateChannel(
        Func<Dictionary<string, object?>, Task> send,
        Func<int, CancellationToken, Task<Msg>> recv)
    {
        _send = send;
        _recv = recv;
    }

    private async Task<Msg> RequestAsync(Dictionary<string, object?> msg, CancellationToken ct)
    {
        await _send(msg).ConfigureAwait(false);
        var resp = await _recv(Ch.State, ct).ConfigureAwait(false);
        if (NdjsonProtocol.GetType(resp) == "error")
            throw new ServerException(
                NdjsonProtocol.GetString(resp, "message", "state error"), Ch.State);
        return resp;
    }

    // --- Variables ---

    public async Task<VarInfo> GetVarAsync(string name, CancellationToken ct = default)
    {
        var resp = await RequestAsync(NdjsonProtocol.MakeMsg(Ch.State, "get",
            new() { ["target"] = "var", ["name"] = name }), ct).ConfigureAwait(false);
        return new VarInfo(
            NdjsonProtocol.GetString(resp, "name", name),
            NdjsonProtocol.GetString(resp, "value"),
            NdjsonProtocol.GetStringList(resp, "attributes"));
    }

    public async Task SetVarAsync(string name, string value,
        string[]? attributes = null, CancellationToken ct = default)
    {
        var extra = new Dictionary<string, object?>
        {
            ["target"] = "var", ["name"] = name, ["value"] = value,
        };
        if (attributes != null && attributes.Length > 0)
            extra["attributes"] = attributes;
        await RequestAsync(NdjsonProtocol.MakeMsg(Ch.State, "set", extra), ct).ConfigureAwait(false);
    }

    public async Task UnsetVarAsync(string name, CancellationToken ct = default)
    {
        await RequestAsync(NdjsonProtocol.MakeMsg(Ch.State, "unset",
            new() { ["target"] = "var", ["name"] = name }), ct).ConfigureAwait(false);
    }

    // --- Functions ---

    public async Task<FuncInfo> GetFuncAsync(string name, CancellationToken ct = default)
    {
        var resp = await RequestAsync(NdjsonProtocol.MakeMsg(Ch.State, "get",
            new() { ["target"] = "function", ["name"] = name }), ct).ConfigureAwait(false);
        return new FuncInfo(
            NdjsonProtocol.GetString(resp, "name", name),
            NdjsonProtocol.GetString(resp, "definition"));
    }

    public async Task UnsetFuncAsync(string name, CancellationToken ct = default)
    {
        await RequestAsync(NdjsonProtocol.MakeMsg(Ch.State, "unset",
            new() { ["target"] = "function", ["name"] = name }), ct).ConfigureAwait(false);
    }

    // --- Aliases ---

    public async Task<AliasInfo> GetAliasAsync(string name, CancellationToken ct = default)
    {
        var resp = await RequestAsync(NdjsonProtocol.MakeMsg(Ch.State, "get",
            new() { ["target"] = "alias", ["name"] = name }), ct).ConfigureAwait(false);
        return new AliasInfo(
            NdjsonProtocol.GetString(resp, "name", name),
            NdjsonProtocol.GetString(resp, "value"));
    }

    public async Task SetAliasAsync(string name, string value, CancellationToken ct = default)
    {
        await RequestAsync(NdjsonProtocol.MakeMsg(Ch.State, "set",
            new() { ["target"] = "alias", ["name"] = name, ["value"] = value }), ct).ConfigureAwait(false);
    }

    public async Task UnsetAliasAsync(string name, CancellationToken ct = default)
    {
        await RequestAsync(NdjsonProtocol.MakeMsg(Ch.State, "unset",
            new() { ["target"] = "alias", ["name"] = name }), ct).ConfigureAwait(false);
    }

    // --- Traps ---

    public async Task SetTrapAsync(string signal, string command, CancellationToken ct = default)
    {
        await RequestAsync(NdjsonProtocol.MakeMsg(Ch.State, "set",
            new() { ["target"] = "trap", ["name"] = signal, ["value"] = command }), ct).ConfigureAwait(false);
    }

    public async Task UnsetTrapAsync(string signal, CancellationToken ct = default)
    {
        await RequestAsync(NdjsonProtocol.MakeMsg(Ch.State, "unset",
            new() { ["target"] = "trap", ["name"] = signal }), ct).ConfigureAwait(false);
    }

    // --- Inspect ---

    public async Task<List<Msg>> InspectAsync(string query, CancellationToken ct = default)
    {
        var resp = await RequestAsync(NdjsonProtocol.MakeMsg(Ch.State, "inspect",
            new() { ["query"] = query }), ct).ConfigureAwait(false);
        return NdjsonProtocol.GetObjectArray(resp, "data");
    }
}
