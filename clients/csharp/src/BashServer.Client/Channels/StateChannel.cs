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
        await _send(msg);
        var resp = await _recv(Ch.State, ct);
        if (NdjsonProtocol.GetType(resp) == "error")
            throw new ServerException(
                NdjsonProtocol.GetString(resp, "message", "state error"), Ch.State);
        return resp;
    }

    // --- Variables ---

    public async Task<VarInfo> GetVarAsync(string name, CancellationToken ct = default)
    {
        var resp = await RequestAsync(NdjsonProtocol.MakeMsg(Ch.State, "get",
            new() { ["target"] = "var", ["name"] = name }), ct);
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
        await RequestAsync(NdjsonProtocol.MakeMsg(Ch.State, "set", extra), ct);
    }

    public async Task UnsetVarAsync(string name, CancellationToken ct = default)
    {
        await RequestAsync(NdjsonProtocol.MakeMsg(Ch.State, "unset",
            new() { ["target"] = "var", ["name"] = name }), ct);
    }

    // --- Functions ---

    public async Task<FuncInfo> GetFuncAsync(string name, CancellationToken ct = default)
    {
        var resp = await RequestAsync(NdjsonProtocol.MakeMsg(Ch.State, "get",
            new() { ["target"] = "function", ["name"] = name }), ct);
        return new FuncInfo(
            NdjsonProtocol.GetString(resp, "name", name),
            NdjsonProtocol.GetString(resp, "definition"));
    }

    public async Task UnsetFuncAsync(string name, CancellationToken ct = default)
    {
        await RequestAsync(NdjsonProtocol.MakeMsg(Ch.State, "unset",
            new() { ["target"] = "function", ["name"] = name }), ct);
    }

    // --- Aliases ---

    public async Task<AliasInfo> GetAliasAsync(string name, CancellationToken ct = default)
    {
        var resp = await RequestAsync(NdjsonProtocol.MakeMsg(Ch.State, "get",
            new() { ["target"] = "alias", ["name"] = name }), ct);
        return new AliasInfo(
            NdjsonProtocol.GetString(resp, "name", name),
            NdjsonProtocol.GetString(resp, "value"));
    }

    public async Task SetAliasAsync(string name, string value, CancellationToken ct = default)
    {
        await RequestAsync(NdjsonProtocol.MakeMsg(Ch.State, "set",
            new() { ["target"] = "alias", ["name"] = name, ["value"] = value }), ct);
    }

    public async Task UnsetAliasAsync(string name, CancellationToken ct = default)
    {
        await RequestAsync(NdjsonProtocol.MakeMsg(Ch.State, "unset",
            new() { ["target"] = "alias", ["name"] = name }), ct);
    }

    // --- Traps ---

    public async Task SetTrapAsync(string signal, string command, CancellationToken ct = default)
    {
        await RequestAsync(NdjsonProtocol.MakeMsg(Ch.State, "set",
            new() { ["target"] = "trap", ["name"] = signal, ["value"] = command }), ct);
    }

    public async Task UnsetTrapAsync(string signal, CancellationToken ct = default)
    {
        await RequestAsync(NdjsonProtocol.MakeMsg(Ch.State, "unset",
            new() { ["target"] = "trap", ["name"] = signal }), ct);
    }

    // --- Inspect ---

    public async Task<List<Msg>> InspectAsync(string query, CancellationToken ct = default)
    {
        var resp = await RequestAsync(NdjsonProtocol.MakeMsg(Ch.State, "inspect",
            new() { ["query"] = query }), ct);
        return NdjsonProtocol.GetObjectArray(resp, "data");
    }
}
