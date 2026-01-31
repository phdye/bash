namespace BashServer.Client;

/// <summary>Transport interface for reading/writing NDJSON lines.</summary>
public interface ITransport
{
    /// <summary>Read one NDJSON line (including trailing newline).</summary>
    Task<string> ReadLineAsync(CancellationToken ct = default);

    /// <summary>Write bytes to the transport.</summary>
    Task WriteAsync(byte[] data, CancellationToken ct = default);

    /// <summary>Close the transport.</summary>
    Task CloseAsync();

    /// <summary>Whether the transport is open.</summary>
    bool IsOpen { get; }
}
