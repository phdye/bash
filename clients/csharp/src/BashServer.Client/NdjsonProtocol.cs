using System.Text;
using System.Text.Json;

namespace BashServer.Client;

/// <summary>NDJSON protocol encode/decode helpers.</summary>
public static class NdjsonProtocol
{
    private static readonly JsonSerializerOptions s_jsonOpts = new()
    {
        PropertyNamingPolicy = null, // We use explicit JsonPropertyName attributes
    };

    /// <summary>Encode a message dict as an NDJSON line.</summary>
    public static byte[] EncodeFrame(Dictionary<string, object?> msg)
    {
        try
        {
            var json = JsonSerializer.Serialize(msg, s_jsonOpts);
            return Encoding.UTF8.GetBytes(json + "\n");
        }
        catch (Exception ex)
        {
            throw new ProtocolException($"cannot encode frame: {ex.Message}", ex);
        }
    }

    /// <summary>Decode an NDJSON line into a message dict.</summary>
    public static Dictionary<string, JsonElement> DecodeFrame(ReadOnlySpan<byte> line)
    {
        var trimmed = Encoding.UTF8.GetString(line).Trim();
        if (string.IsNullOrEmpty(trimmed))
            throw new ProtocolException("empty frame");
        if (trimmed.Length > Protocol.FrameMaxPayload)
            throw new ProtocolException(
                $"frame exceeds max payload ({trimmed.Length} > {Protocol.FrameMaxPayload})");
        try
        {
            var doc = JsonDocument.Parse(trimmed);
            if (doc.RootElement.ValueKind != JsonValueKind.Object)
                throw new ProtocolException(
                    $"expected JSON object, got {doc.RootElement.ValueKind}");

            var result = new Dictionary<string, JsonElement>();
            foreach (var prop in doc.RootElement.EnumerateObject())
            {
                result[prop.Name] = prop.Value.Clone();
            }
            return result;
        }
        catch (JsonException ex)
        {
            throw new ProtocolException($"invalid JSON: {ex.Message}", ex);
        }
    }

    /// <summary>Decode an NDJSON string line.</summary>
    public static Dictionary<string, JsonElement> DecodeFrame(string line)
        => DecodeFrame(Encoding.UTF8.GetBytes(line));

    /// <summary>Base64-encode a string.</summary>
    public static string B64Encode(string data)
        => Convert.ToBase64String(Encoding.UTF8.GetBytes(data));

    /// <summary>Base64-decode a string.</summary>
    public static string B64Decode(string data)
    {
        try
        {
            return Encoding.UTF8.GetString(Convert.FromBase64String(data));
        }
        catch (Exception ex)
        {
            throw new ProtocolException($"base64 decode error: {ex.Message}", ex);
        }
    }

    /// <summary>Build a protocol message.</summary>
    public static Dictionary<string, object?> MakeMsg(int channel, string type,
        Dictionary<string, object?>? extra = null)
    {
        var msg = new Dictionary<string, object?>
        {
            ["ch"] = channel,
            ["type"] = type,
        };
        if (extra != null)
        {
            foreach (var kv in extra)
                msg[kv.Key] = kv.Value;
        }
        return msg;
    }

    /// <summary>Extract channel ID from message, defaulting to 0.</summary>
    public static int GetChannel(Dictionary<string, JsonElement> msg)
    {
        if (msg.TryGetValue("ch", out var ch) && ch.ValueKind == JsonValueKind.Number)
            return ch.GetInt32();
        return 0;
    }

    /// <summary>Extract message type.</summary>
    public static string GetType(Dictionary<string, JsonElement> msg)
    {
        if (msg.TryGetValue("type", out var t) && t.ValueKind == JsonValueKind.String)
            return t.GetString()!;
        if (!msg.ContainsKey("type"))
            throw new ProtocolException("message has no 'type' field");
        return msg["type"].ToString();
    }

    /// <summary>Get a string field safely.</summary>
    public static string GetString(Dictionary<string, JsonElement> msg, string key,
        string defaultValue = "")
    {
        if (msg.TryGetValue(key, out var v) && v.ValueKind == JsonValueKind.String)
            return v.GetString() ?? defaultValue;
        return defaultValue;
    }

    /// <summary>Get an int field safely.</summary>
    public static int GetInt(Dictionary<string, JsonElement> msg, string key,
        int defaultValue = 0)
    {
        if (msg.TryGetValue(key, out var v) && v.ValueKind == JsonValueKind.Number)
            return v.GetInt32();
        return defaultValue;
    }

    /// <summary>Get a long field safely.</summary>
    public static long GetLong(Dictionary<string, JsonElement> msg, string key,
        long defaultValue = 0)
    {
        if (msg.TryGetValue(key, out var v) && v.ValueKind == JsonValueKind.Number)
            return v.GetInt64();
        return defaultValue;
    }

    /// <summary>Get a bool field safely.</summary>
    public static bool GetBool(Dictionary<string, JsonElement> msg, string key,
        bool defaultValue = false)
    {
        if (msg.TryGetValue(key, out var v))
        {
            if (v.ValueKind == JsonValueKind.True) return true;
            if (v.ValueKind == JsonValueKind.False) return false;
        }
        return defaultValue;
    }

    /// <summary>Get a string list field safely.</summary>
    public static List<string> GetStringList(Dictionary<string, JsonElement> msg, string key)
    {
        var result = new List<string>();
        if (msg.TryGetValue(key, out var v) && v.ValueKind == JsonValueKind.Array)
        {
            foreach (var item in v.EnumerateArray())
            {
                if (item.ValueKind == JsonValueKind.String)
                    result.Add(item.GetString()!);
            }
        }
        return result;
    }

    /// <summary>Get a nested object as a dictionary.</summary>
    public static Dictionary<string, JsonElement>? GetObject(
        Dictionary<string, JsonElement> msg, string key)
    {
        if (msg.TryGetValue(key, out var v) && v.ValueKind == JsonValueKind.Object)
        {
            var result = new Dictionary<string, JsonElement>();
            foreach (var prop in v.EnumerateObject())
                result[prop.Name] = prop.Value.Clone();
            return result;
        }
        return null;
    }

    /// <summary>Get an array field.</summary>
    public static List<Dictionary<string, JsonElement>> GetObjectArray(
        Dictionary<string, JsonElement> msg, string key)
    {
        var result = new List<Dictionary<string, JsonElement>>();
        if (msg.TryGetValue(key, out var v) && v.ValueKind == JsonValueKind.Array)
        {
            foreach (var item in v.EnumerateArray())
            {
                if (item.ValueKind == JsonValueKind.Object)
                {
                    var dict = new Dictionary<string, JsonElement>();
                    foreach (var prop in item.EnumerateObject())
                        dict[prop.Name] = prop.Value.Clone();
                    result.Add(dict);
                }
            }
        }
        return result;
    }
}
