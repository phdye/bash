using System.Diagnostics;

namespace BashServer.Client;

/// <summary>Transport over subprocess stdin/stdout (--stdio mode).</summary>
public class StdioTransport : ITransport
{
    private Process? _process;
    private StreamReader? _reader;
    private StreamWriter? _writer;
    private volatile bool _open;

    public Task ConnectProcessAsync(params string[] args)
    {
        if (args.Length == 0)
            throw new TransportException("no command specified");

        try
        {
            var psi = new ProcessStartInfo
            {
                FileName = args[0],
                RedirectStandardInput = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false,
                CreateNoWindow = true,
            };
            for (int i = 1; i < args.Length; i++)
                psi.ArgumentList.Add(args[i]);

            _process = Process.Start(psi)
                ?? throw new TransportException("failed to start process");
            _reader = _process.StandardOutput;
            _writer = _process.StandardInput;
            _open = true;
            return Task.CompletedTask;
        }
        catch (Exception ex) when (ex is not TransportException)
        {
            throw new TransportException($"cannot start process: {ex.Message}", ex);
        }
    }

    public async Task<string> ReadLineAsync(CancellationToken ct = default)
    {
        if (_reader == null) throw new TransportException("not connected");
        try
        {
            var line = await _reader.ReadLineAsync().ConfigureAwait(false);
            if (line == null)
            {
                _open = false;
                throw new TransportException("connection closed");
            }
            return line + "\n";
        }
        catch (Exception ex) when (ex is IOException)
        {
            _open = false;
            throw new TransportException($"read error: {ex.Message}", ex);
        }
    }

    public async Task WriteAsync(byte[] data, CancellationToken ct = default)
    {
        if (_writer == null) throw new TransportException("not connected");
        try
        {
            await _writer.BaseStream.WriteAsync(data, ct).ConfigureAwait(false);
            await _writer.BaseStream.FlushAsync(ct).ConfigureAwait(false);
        }
        catch (Exception ex) when (ex is IOException)
        {
            _open = false;
            throw new TransportException($"write error: {ex.Message}", ex);
        }
    }

    public Task CloseAsync()
    {
        _open = false;
        try
        {
            if (_process != null && !_process.HasExited)
            {
                _process.Kill();
                _process.WaitForExit(5000);
            }
        }
        catch (InvalidOperationException) { /* Process already exited. */ }
        _process?.Dispose();
        return Task.CompletedTask;
    }

    public bool IsOpen => _open;
}
