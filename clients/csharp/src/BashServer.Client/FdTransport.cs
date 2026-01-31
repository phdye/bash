using Microsoft.Win32.SafeHandles;

namespace BashServer.Client;

/// <summary>Transport over an inherited file descriptor.</summary>
public class FdTransport : ITransport
{
    private FileStream? _stream;
    private StreamReader? _reader;
    private volatile bool _open;

    public Task ConnectAsync(int fd)
    {
        try
        {
            var handle = new SafeFileHandle((IntPtr)fd, ownsHandle: false);
            _stream = new FileStream(handle, FileAccess.ReadWrite);
            _reader = new StreamReader(_stream);
            _open = true;
            return Task.CompletedTask;
        }
        catch (Exception ex)
        {
            throw new TransportException($"cannot open fd {fd}: {ex.Message}", ex);
        }
    }

    public async Task<string> ReadLineAsync(CancellationToken ct = default)
    {
        if (_reader == null) throw new TransportException("not connected");
        try
        {
            var line = await _reader.ReadLineAsync();
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
        if (_stream == null) throw new TransportException("not connected");
        try
        {
            await _stream.WriteAsync(data, ct);
            await _stream.FlushAsync(ct);
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
        if (_stream != null) await _stream.DisposeAsync();
    }

    public bool IsOpen => _open;
}
