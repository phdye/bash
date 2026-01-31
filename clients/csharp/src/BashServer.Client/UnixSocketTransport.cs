using System.Net.Sockets;

namespace BashServer.Client;

/// <summary>Unix domain socket transport.</summary>
public class UnixSocketTransport : ITransport
{
    private Socket? _socket;
    private StreamReader? _reader;
    private NetworkStream? _stream;
    private volatile bool _open;

    public async Task ConnectAsync(string path, CancellationToken ct = default)
    {
        try
        {
            _socket = new Socket(AddressFamily.Unix, SocketType.Stream, ProtocolType.Unspecified);
            await _socket.ConnectAsync(new UnixDomainSocketEndPoint(path), ct);
            _stream = new NetworkStream(_socket, ownsSocket: true);
            _reader = new StreamReader(_stream);
            _open = true;
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            throw new TransportException($"cannot connect to {path}: {ex.Message}", ex);
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
        catch (Exception ex) when (ex is IOException or SocketException)
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
        catch (Exception ex) when (ex is IOException or SocketException)
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
        _socket?.Dispose();
    }

    public bool IsOpen => _open;
}
