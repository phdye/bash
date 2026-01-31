using BashServer.Client;
using BashServer.Client.Channels;
using Xunit;

namespace BashServer.Client.Tests;

public class ChannelTests : IAsyncLifetime
{
    private MockTransport _transport = null!;
    private MockServer _server = null!;
    private BashClient _client = null!;

    public Task InitializeAsync()
    {
        _transport = new MockTransport();
        _server = new MockServer(_transport);
        _server.Start();
        _client = BashClient.FromTransport(_transport);
        return Task.CompletedTask;
    }

    public async Task DisposeAsync()
    {
        await _server.StopAsync();
        await _client.DisposeAsync();
    }

    [Fact]
    public async Task Auth_Success()
    {
        await _client.AuthAsync("valid-token");
        Assert.True(_client.IsAuthenticated);
    }

    [Fact]
    public async Task Auth_Failure()
    {
        await Assert.ThrowsAsync<AuthException>(
            () => _client.AuthAsync("wrong-token"));
        Assert.False(_client.IsAuthenticated);
    }

    [Fact]
    public async Task Ping()
    {
        await _client.PingAsync();
    }

    [Fact]
    public async Task Eval_ReturnsResult()
    {
        var result = await _client.EvalAsync("echo hello");
        Assert.Contains("echo hello", result.Stdout);
        Assert.Equal(0, result.ExitCode);
    }

    [Fact]
    public async Task State_GetVar()
    {
        var info = await _client.State.GetVarAsync("HOME");
        Assert.Equal("HOME", info.Name);
        Assert.Equal("test-value", info.Value);
    }

    [Fact]
    public async Task State_SetVar()
    {
        await _client.State.SetVarAsync("FOO", "bar");
        // no exception = success
    }

    [Fact]
    public async Task State_UnsetVar()
    {
        await _client.State.UnsetVarAsync("FOO");
    }

    [Fact]
    public async Task State_GetFunc()
    {
        var info = await _client.State.GetFuncAsync("myfunc");
        Assert.Equal("myfunc", info.Name);
        Assert.Contains("echo hello", info.Definition);
    }

    [Fact]
    public async Task State_GetAlias()
    {
        var info = await _client.State.GetAliasAsync("ll");
        Assert.Equal("ll", info.Name);
        Assert.Equal("ls -la", info.Value);
    }

    [Fact]
    public async Task Observe_SubscribeUnsubscribe()
    {
        await _client.Observe.SubscribeAsync();
        await _client.Observe.UnsubscribeAsync();
    }

    [Fact]
    public async Task Debug_EnableDisable()
    {
        await _client.Debug.EnableAsync();
        await _client.Debug.DisableAsync();
    }

    [Fact]
    public async Task Debug_Status()
    {
        var status = await _client.Debug.StatusAsync();
        Assert.False(status.Active);
        Assert.Equal("run", status.Mode);
    }

    [Fact]
    public async Task Pty_Spawn()
    {
        var info = await _client.Pty.SpawnAsync();
        Assert.Equal(24, info.Rows);
        Assert.Equal(80, info.Cols);
        Assert.Equal(12345, info.Pid);
    }

    [Fact]
    public async Task Observe_Dispatch_PreCommand()
    {
        var tcs = new TaskCompletionSource<PreCommandEvent>();
        _client.Observe.PreCommand += (_, e) => tcs.TrySetResult(e);

        // Inject a server-push pre_command message
        _transport.InjectServerMsg(Ch.Observe, "pre_command", new()
        {
            ["seq"] = 1, ["timestamp"] = 1700000000L,
            ["data"] = new Dictionary<string, object?>
            {
                ["command"] = "ls", ["cwd"] = "/tmp",
                ["line_number"] = 1, ["is_subshell"] = false, ["is_async"] = false
            }
        });

        using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        var evt = await tcs.Task.WaitAsync(cts.Token);
        Assert.Equal(1, evt.Seq);
        Assert.Equal("ls", evt.Command);
    }

    [Fact]
    public async Task Pty_Dispatch_Output()
    {
        var tcs = new TaskCompletionSource<string>();
        _client.Pty.Output += (_, data) => tcs.TrySetResult(data);

        var b64 = NdjsonProtocol.B64Encode("hello pty\n");
        _transport.InjectServerMsg(Ch.Pty, "output", new()
        {
            ["data"] = b64, ["encoding"] = "base64"
        });

        using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        var output = await tcs.Task.WaitAsync(cts.Token);
        Assert.Equal("hello pty\n", output);
    }

    [Fact]
    public async Task Debug_Dispatch_BreakHit()
    {
        var tcs = new TaskCompletionSource<BreakHitEvent>();
        _client.Debug.BreakHit += (_, e) => tcs.TrySetResult(e);

        _transport.InjectServerMsg(Ch.Debug, "break_hit", new()
        {
            ["line"] = 42, ["command"] = "echo test", ["depth"] = 0
        });

        using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        var evt = await tcs.Task.WaitAsync(cts.Token);
        Assert.Equal(42, evt.Line);
        Assert.Equal("echo test", evt.Command);
    }
}
