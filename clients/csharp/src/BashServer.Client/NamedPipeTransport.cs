using System.IO.Pipes;

namespace BashServer.Client;

/// <summary>Windows Named Pipe transport.</summary>
public class NamedPipeTransport : ITransport
{
    private NamedPipeClientStream? _pipe;
    private StreamReader? _reader;
    private volatile bool _open;

    public async Task ConnectAsync(string pipeName, CancellationToken ct = default)
    {
        try
        {
            // Parse pipe name: \\.\pipe\name -> serverName=".", pipeName="name"
            var serverName = ".";
            var name = pipeName;
            if (pipeName.StartsWith(@"\\"))
            {
                var parts = pipeName.Split('\\', StringSplitOptions.RemoveEmptyEntries);
                if (parts.Length >= 3)
                {
                    serverName = parts[0];
                    name = string.Join('\\', parts.Skip(2));
                }
            }

            _pipe = new NamedPipeClientStream(serverName, name,
                PipeDirection.InOut, PipeOptions.Asynchronous);
            await _pipe.ConnectAsync(ct).ConfigureAwait(false);
            _reader = new StreamReader(_pipe);
            _open = true;
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            throw new TransportException($"cannot connect to pipe {pipeName}: {ex.Message}", ex);
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
        if (_pipe == null) throw new TransportException("not connected");
        try
        {
            await _pipe.WriteAsync(data, ct).ConfigureAwait(false);
            await _pipe.FlushAsync(ct).ConfigureAwait(false);
        }
        catch (Exception ex) when (ex is IOException)
        {
            _open = false;
            throw new TransportException($"write error: {ex.Message}", ex);
        }
    }

    public async Task CloseAsync()
    {
        _open = false;
        _reader?.Dispose();
        if (_pipe != null) await _pipe.DisposeAsync().ConfigureAwait(false);
    }

    public bool IsOpen => _open;
}
