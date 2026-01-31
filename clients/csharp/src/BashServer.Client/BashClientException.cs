namespace BashServer.Client;

/// <summary>Base exception for all bashclient errors.</summary>
public class BashClientException : Exception
{
    public BashClientException(string message) : base(message) { }
    public BashClientException(string message, Exception inner) : base(message, inner) { }
}

/// <summary>Authentication failed.</summary>
public class AuthException : BashClientException
{
    public AuthException(string message) : base(message) { }
}

/// <summary>Malformed frame or message.</summary>
public class ProtocolException : BashClientException
{
    public ProtocolException(string message) : base(message) { }
    public ProtocolException(string message, Exception inner) : base(message, inner) { }
}

/// <summary>Operation timed out.</summary>
public class TimeoutException : BashClientException
{
    public TimeoutException(string message) : base(message) { }
}

/// <summary>Socket or I/O error.</summary>
public class TransportException : BashClientException
{
    public TransportException(string message) : base(message) { }
    public TransportException(string message, Exception inner) : base(message, inner) { }
}

/// <summary>Server returned an error response.</summary>
public class ServerException : BashClientException
{
    public int Channel { get; }

    public ServerException(string message, int channel = -1) : base(message)
    {
        Channel = channel;
    }
}
