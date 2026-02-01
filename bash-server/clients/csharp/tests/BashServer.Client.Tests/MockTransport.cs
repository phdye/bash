using System.Collections.Concurrent;
using System.Text;
using System.Text.Json;
using BashServer.Client;

namespace BashServer.Client.Tests;

/// <summary>
/// In-memory transport for testing. Lines written by the client can be
/// read via <see cref="ReadClientLine"/>, and server responses can be
/// injected via <see cref="InjectServerLine"/>.
/// </summary>
public sealed class MockTransport : ITransport
{
    private readonly ConcurrentQueue<string> _serverLines = new();
    private readonly ConcurrentQueue<string> _clientLines = new();
    private readonly SemaphoreSlim _serverSem = new(0);
    private bool _open = true;

    public bool IsOpen => _open;

    public async Task<string> ReadLineAsync(CancellationToken ct = default)
    {
        while (true)
        {
            ct.ThrowIfCancellationRequested();
            if (!_open) throw new TransportException("transport closed");
            if (_serverLines.TryDequeue(out var line))
                return line;
            await _serverSem.WaitAsync(ct);
        }
    }

    public Task WriteAsync(byte[] data, CancellationToken ct = default)
    {
        if (!_open) throw new TransportException("transport closed");
        _clientLines.Enqueue(Encoding.UTF8.GetString(data));
        return Task.CompletedTask;
    }

    public Task CloseAsync()
    {
        _open = false;
        _serverSem.Release(10); // unblock any waiters
        return Task.CompletedTask;
    }

    /// <summary>Inject a server-side NDJSON line for the client to read.</summary>
    public void InjectServerLine(string json)
    {
        _serverLines.Enqueue(json + "\n");
        _serverSem.Release();
    }

    /// <summary>Inject a server response message.</summary>
    public void InjectServerMsg(int channel, string type,
        Dictionary<string, object?>? extra = null)
    {
        var msg = NdjsonProtocol.MakeMsg(channel, type, extra);
        var bytes = NdjsonProtocol.EncodeFrame(msg);
        var line = Encoding.UTF8.GetString(bytes);
        _serverLines.Enqueue(line);
        _serverSem.Release();
    }

    /// <summary>Read the next line written by the client.</summary>
    public string? ReadClientLine()
    {
        _clientLines.TryDequeue(out var line);
        return line;
    }
}

/// <summary>
/// Auto-responding mock server that handles common protocol flows.
/// Call <see cref="Start"/> to begin auto-responding in background.
/// </summary>
public sealed class MockServer
{
    private readonly MockTransport _transport;
    private CancellationTokenSource? _cts;
    private Task? _task;

    public MockServer(MockTransport transport) => _transport = transport;

    public void Start()
    {
        _cts = new CancellationTokenSource();
        _task = Task.Run(() => RunAsync(_cts.Token));
    }

    public async Task StopAsync()
    {
        _cts?.Cancel();
        if (_task != null)
            try { await _task; } catch { }
    }

    private async Task RunAsync(CancellationToken ct)
    {
        try
        {
            while (!ct.IsCancellationRequested && _transport.IsOpen)
            {
                string? line;
                while ((line = _transport.ReadClientLine()) == null)
                {
                    ct.ThrowIfCancellationRequested();
                    await Task.Delay(10, ct);
                }

                Dictionary<string, JsonElement> msg;
                try { msg = NdjsonProtocol.DecodeFrame(line); }
                catch { continue; }

                int ch = NdjsonProtocol.GetChannel(msg);
                string type = NdjsonProtocol.GetType(msg);

                switch (ch)
                {
                    case Ch.Control:
                        HandleControl(type, msg);
                        break;
                    case Ch.Command:
                        HandleCommand(type, msg);
                        break;
                    case Ch.State:
                        HandleState(type, msg);
                        break;
                    case Ch.Observe:
                        HandleObserve(type, msg);
                        break;
                    case Ch.Debug:
                        HandleDebug(type, msg);
                        break;
                    case Ch.Pty:
                        HandlePty(type, msg);
                        break;
                }
            }
        }
        catch (OperationCanceledException) { }
    }

    private void HandleControl(string type, Dictionary<string, JsonElement> msg)
    {
        switch (type)
        {
            case "auth":
                var token = NdjsonProtocol.GetString(msg, "token");
                if (token == "valid-token")
                    _transport.InjectServerMsg(0, "auth_ok");
                else
                    _transport.InjectServerMsg(0, "error",
                        new() { ["message"] = "authentication failed" });
                break;
            case "ping":
                _transport.InjectServerMsg(0, "pong");
                break;
            case "disconnect":
                _transport.InjectServerMsg(0, "disconnect_ok");
                break;
            case "configure":
                _transport.InjectServerMsg(0, "configure_ok");
                break;
        }
    }

    private void HandleCommand(string type, Dictionary<string, JsonElement> msg)
    {
        if (type == "eval")
        {
            var cmd = NdjsonProtocol.GetString(msg, "command");
            var b64out = NdjsonProtocol.B64Encode($"output of: {cmd}\n");
            _transport.InjectServerMsg(1, "stdout",
                new() { ["data"] = b64out, ["encoding"] = "base64" });
            _transport.InjectServerMsg(1, "stderr",
                new() { ["data"] = "", ["encoding"] = "base64" });
            _transport.InjectServerMsg(1, "complete",
                new() { ["exit_code"] = 0 });
        }
    }

    private void HandleState(string type, Dictionary<string, JsonElement> msg)
    {
        var target = NdjsonProtocol.GetString(msg, "target");
        switch (type)
        {
            case "get":
                switch (target)
                {
                    case "var":
                        _transport.InjectServerMsg(2, "var_value", new()
                        {
                            ["name"] = NdjsonProtocol.GetString(msg, "name"),
                            ["value"] = "test-value",
                            ["attributes"] = new List<string>()
                        });
                        break;
                    case "function":
                        _transport.InjectServerMsg(2, "func_value", new()
                        {
                            ["name"] = NdjsonProtocol.GetString(msg, "name"),
                            ["definition"] = "() { echo hello; }"
                        });
                        break;
                    case "alias":
                        _transport.InjectServerMsg(2, "alias_value", new()
                        {
                            ["name"] = NdjsonProtocol.GetString(msg, "name"),
                            ["value"] = "ls -la"
                        });
                        break;
                }
                break;
            case "set":
            case "unset":
                _transport.InjectServerMsg(2, "ok");
                break;
            case "inspect":
                _transport.InjectServerMsg(2, "inspect_result", new()
                {
                    ["data"] = new List<Dictionary<string, object?>>
                    {
                        new() { ["name"] = "HOME", ["type"] = "variable" }
                    }
                });
                break;
            default:
                _transport.InjectServerMsg(2, "ok");
                break;
        }
    }

    private void HandleObserve(string type, Dictionary<string, JsonElement> msg)
    {
        switch (type)
        {
            case "subscribe":
                _transport.InjectServerMsg(3, "subscribed");
                break;
            case "unsubscribe":
                _transport.InjectServerMsg(3, "unsubscribed");
                break;
        }
    }

    private void HandleDebug(string type, Dictionary<string, JsonElement> msg)
    {
        switch (type)
        {
            case "enable":
                _transport.InjectServerMsg(4, "enabled");
                break;
            case "disable":
                _transport.InjectServerMsg(4, "disabled");
                break;
            case "status":
                _transport.InjectServerMsg(4, "status", new()
                {
                    ["active"] = false, ["mode"] = "run",
                    ["breakpoints"] = 0, ["depth"] = 0
                });
                break;
            default:
                _transport.InjectServerMsg(4, "ok");
                break;
        }
    }

    private void HandlePty(string type, Dictionary<string, JsonElement> msg)
    {
        switch (type)
        {
            case "spawn":
                _transport.InjectServerMsg(5, "spawned", new()
                {
                    ["rows"] = 24, ["cols"] = 80, ["pid"] = 12345,
                    ["strip_ansi"] = false
                });
                break;
            case "resize":
                _transport.InjectServerMsg(5, "resized");
                break;
            case "signal":
                _transport.InjectServerMsg(5, "signaled");
                break;
        }
    }
}
