using System.Text.Json;
using BashServer.Client.Channels;
using Msg = System.Collections.Generic.Dictionary<string, System.Text.Json.JsonElement>;

namespace BashServer.Client;

/// <summary>
/// Async client for bash-server v2 NDJSON protocol.
/// Implements IAsyncDisposable for use with <c>await using</c>.
/// </summary>
public sealed class BashClient : IAsyncDisposable
{
    private readonly ITransport _transport;
    private readonly AsyncQueue[] _channelQueues;
    private Task? _readerTask;
    private CancellationTokenSource? _readerCts;
    private bool _authenticated;
    private bool _disposed;

    /// <summary>Channel 0: auth, ping, configure, disconnect.</summary>
    public ControlChannel Control { get; }

    /// <summary>Channel 1: command evaluation.</summary>
    public CommandChannel Command { get; }

    /// <summary>Channel 2: variable/function/alias/trap operations.</summary>
    public StateChannel State { get; }

    /// <summary>Channel 3: pre/post command observation.</summary>
    public ObserveChannel Observe { get; }

    /// <summary>Channel 4: debugging (breakpoints, stepping, AST).</summary>
    public DebugChannel Debug { get; }

    /// <summary>Channel 5: pseudo-terminal I/O.</summary>
    public PtyChannel Pty { get; }

    /// <summary>Whether the transport is open.</summary>
    public bool IsConnected => _transport.IsOpen;

    /// <summary>Whether authentication has succeeded.</summary>
    public bool IsAuthenticated => _authenticated;

    private BashClient(ITransport transport)
    {
        _transport = transport;
        _channelQueues = new AsyncQueue[6];
        for (int i = 0; i < 6; i++)
            _channelQueues[i] = new AsyncQueue();

        Control = new ControlChannel(SendAsync, RecvAsync);
        Command = new CommandChannel(SendAsync, RecvAsync);
        State = new StateChannel(SendAsync, RecvAsync);
        Observe = new ObserveChannel(SendAsync, RecvAsync);
        Debug = new DebugChannel(SendAsync, RecvAsync);
        Pty = new PtyChannel(SendAsync, RecvAsync);
    }

    /// <summary>Connect via Unix domain socket.</summary>
    public static async Task<BashClient> ConnectAsync(string socketPath, CancellationToken ct = default)
    {
        var transport = new UnixSocketTransport();
        await transport.ConnectAsync(socketPath, ct).ConfigureAwait(false);
        var client = new BashClient(transport);
        client.StartReader();
        return client;
    }

    /// <summary>Connect via subprocess stdin/stdout (--stdio mode).</summary>
    public static async Task<BashClient> ConnectStdioAsync(string[] args, CancellationToken ct = default)
    {
        var transport = new StdioTransport();
        await transport.ConnectProcessAsync(args).ConfigureAwait(false);
        var client = new BashClient(transport);
        client.StartReader();
        return client;
    }

    /// <summary>Connect via inherited file descriptor.</summary>
    public static async Task<BashClient> ConnectFdAsync(int fd, CancellationToken ct = default)
    {
        var transport = new FdTransport();
        await transport.ConnectAsync(fd).ConfigureAwait(false);
        var client = new BashClient(transport);
        client.StartReader();
        return client;
    }

    /// <summary>Connect via Windows Named Pipe.</summary>
    public static async Task<BashClient> ConnectNamedPipeAsync(string pipeName, CancellationToken ct = default)
    {
        var transport = new NamedPipeTransport();
        await transport.ConnectAsync(pipeName, ct).ConfigureAwait(false);
        var client = new BashClient(transport);
        client.StartReader();
        return client;
    }

    /// <summary>Create client from an existing transport.</summary>
    public static BashClient FromTransport(ITransport transport)
    {
        var client = new BashClient(transport);
        client.StartReader();
        return client;
    }

    /// <summary>Authenticate with the server.</summary>
    public async Task AuthAsync(string token, CancellationToken ct = default)
    {
        await Control.AuthAsync(token, ct).ConfigureAwait(false);
        _authenticated = true;
    }

    /// <summary>Evaluate a command (convenience wrapper).</summary>
    public Task<EvalResult> EvalAsync(string command, CancellationToken ct = default)
        => Command.EvalAsync(command, ct);

    /// <summary>Ping the server.</summary>
    public Task PingAsync(CancellationToken ct = default)
        => Control.PingAsync(ct);

    /// <summary>Close the connection gracefully.</summary>
    public async Task CloseAsync()
    {
        if (_disposed) return;

        // Send disconnect while reader is still active (best-effort).
        try
        {
            using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(2));
            await Control.DisconnectAsync(cts.Token).ConfigureAwait(false);
        }
        catch (Exception) { /* Disconnect is best-effort on close. */ }

        // Cancel the reader.
        _readerCts?.Cancel();
        if (_readerTask != null)
        {
            try { await _readerTask.ConfigureAwait(false); }
            catch (OperationCanceledException) { }
            catch (TransportException) { }
        }

        await _transport.CloseAsync().ConfigureAwait(false);
    }

    public async ValueTask DisposeAsync()
    {
        if (_disposed) return;
        _disposed = true;
        await CloseAsync().ConfigureAwait(false);
    }

    private void StartReader()
    {
        _readerCts = new CancellationTokenSource();
        _readerTask = Task.Run(() => ReaderLoopAsync(_readerCts.Token));
    }

    private async Task ReaderLoopAsync(CancellationToken ct)
    {
        try
        {
            while (_transport.IsOpen && !ct.IsCancellationRequested)
            {
                string line;
                try
                {
                    line = await _transport.ReadLineAsync(ct).ConfigureAwait(false);
                }
                catch (OperationCanceledException) { break; }
                catch (TransportException) { break; }
                catch (IOException) { break; }

                Msg msg;
                try
                {
                    msg = NdjsonProtocol.DecodeFrame(line);
                }
                catch (ProtocolException) { continue; }

                int ch = NdjsonProtocol.GetChannel(msg);
                string type = NdjsonProtocol.GetType(msg);

                // Server-push messages go to dispatch
                if (ch == Ch.Observe && type is "pre_command" or "post_command")
                {
                    Observe.Dispatch(msg);
                }
                else if (ch == Ch.Debug && type == "break_hit")
                {
                    Debug.Dispatch(msg);
                }
                else if (ch == Ch.Pty && type is "output" or "exit")
                {
                    Pty.Dispatch(msg);
                }
                else if (ch >= 0 && ch <= Ch.Max)
                {
                    // Request/response messages go to channel queue
                    _channelQueues[ch].Enqueue(msg);
                }
            }
        }
        catch (OperationCanceledException) { }
    }

    private Task SendAsync(Dictionary<string, object?> msg)
    {
        var data = NdjsonProtocol.EncodeFrame(msg);
        return _transport.WriteAsync(data);
    }

    private async Task<Msg> RecvAsync(int channel, CancellationToken ct)
    {
        try
        {
            return await _channelQueues[channel].DequeueAsync(ct).ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
            throw new TimeoutException($"timeout waiting for response on channel {channel}");
        }
    }

    /// <summary>Simple async queue for channel message routing.</summary>
    private sealed class AsyncQueue
    {
        private readonly Queue<Msg> _items = new();
        private readonly Queue<TaskCompletionSource<Msg>> _waiters = new();
        private readonly object _lock = new();

        public void Enqueue(Msg item)
        {
            TaskCompletionSource<Msg>? waiter = null;
            lock (_lock)
            {
                if (_waiters.Count > 0)
                    waiter = _waiters.Dequeue();
                else
                    _items.Enqueue(item);
            }
            waiter?.TrySetResult(item);
        }

        public Task<Msg> DequeueAsync(CancellationToken ct)
        {
            lock (_lock)
            {
                if (_items.Count > 0)
                    return Task.FromResult(_items.Dequeue());

                var tcs = new TaskCompletionSource<Msg>(TaskCreationOptions.RunContinuationsAsynchronously);
                ct.Register(() => tcs.TrySetCanceled(ct));
                _waiters.Enqueue(tcs);
                return tcs.Task;
            }
        }
    }
}
