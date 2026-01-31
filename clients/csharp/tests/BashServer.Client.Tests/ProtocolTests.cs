using BashServer.Client;
using Xunit;

namespace BashServer.Client.Tests;

public class ProtocolTests
{
    [Fact]
    public void EncodeFrame_ProducesNdjsonLine()
    {
        var msg = NdjsonProtocol.MakeMsg(0, "ping");
        var bytes = NdjsonProtocol.EncodeFrame(msg);
        var line = System.Text.Encoding.UTF8.GetString(bytes);
        Assert.EndsWith("\n", line);
        Assert.Contains("\"ch\":0", line);
        Assert.Contains("\"type\":\"ping\"", line);
    }

    [Fact]
    public void DecodeFrame_ParsesValidJson()
    {
        var msg = NdjsonProtocol.DecodeFrame("{\"ch\":1,\"type\":\"eval\",\"command\":\"ls\"}");
        Assert.Equal(1, NdjsonProtocol.GetChannel(msg));
        Assert.Equal("eval", NdjsonProtocol.GetType(msg));
        Assert.Equal("ls", NdjsonProtocol.GetString(msg, "command"));
    }

    [Fact]
    public void DecodeFrame_TrimsWhitespace()
    {
        var msg = NdjsonProtocol.DecodeFrame("  {\"ch\":0,\"type\":\"pong\"}  \n");
        Assert.Equal("pong", NdjsonProtocol.GetType(msg));
    }

    [Fact]
    public void DecodeFrame_ThrowsOnEmpty()
    {
        Assert.Throws<ProtocolException>(() => NdjsonProtocol.DecodeFrame(""));
        Assert.Throws<ProtocolException>(() => NdjsonProtocol.DecodeFrame("  \n"));
    }

    [Fact]
    public void DecodeFrame_ThrowsOnInvalidJson()
    {
        Assert.Throws<ProtocolException>(() => NdjsonProtocol.DecodeFrame("{invalid}"));
    }

    [Fact]
    public void DecodeFrame_ThrowsOnNonObject()
    {
        Assert.Throws<ProtocolException>(() => NdjsonProtocol.DecodeFrame("[1,2,3]"));
    }

    [Fact]
    public void DecodeFrame_ThrowsOnOversizedFrame()
    {
        var huge = "{\"ch\":0,\"type\":\"" + new string('x', Protocol.FrameMaxPayload + 1) + "\"}";
        Assert.Throws<ProtocolException>(() => NdjsonProtocol.DecodeFrame(huge));
    }

    [Fact]
    public void RoundTrip_EncodeDecode()
    {
        var original = NdjsonProtocol.MakeMsg(2, "get_var",
            new() { ["name"] = "HOME" });
        var bytes = NdjsonProtocol.EncodeFrame(original);
        var decoded = NdjsonProtocol.DecodeFrame(bytes);
        Assert.Equal(2, NdjsonProtocol.GetChannel(decoded));
        Assert.Equal("get_var", NdjsonProtocol.GetType(decoded));
        Assert.Equal("HOME", NdjsonProtocol.GetString(decoded, "name"));
    }

    [Fact]
    public void B64Encode_ProducesCorrectOutput()
    {
        Assert.Equal("aGVsbG8=", NdjsonProtocol.B64Encode("hello"));
    }

    [Fact]
    public void B64Decode_ProducesCorrectOutput()
    {
        Assert.Equal("hello", NdjsonProtocol.B64Decode("aGVsbG8="));
    }

    [Fact]
    public void B64Decode_ThrowsOnInvalid()
    {
        Assert.Throws<ProtocolException>(() => NdjsonProtocol.B64Decode("!!!"));
    }

    [Fact]
    public void B64RoundTrip()
    {
        const string text = "line1\nline2\ttab";
        Assert.Equal(text, NdjsonProtocol.B64Decode(NdjsonProtocol.B64Encode(text)));
    }

    [Fact]
    public void MakeMsg_SetsChannelAndType()
    {
        var msg = NdjsonProtocol.MakeMsg(3, "subscribe");
        Assert.Equal(3, msg["ch"]);
        Assert.Equal("subscribe", msg["type"]);
    }

    [Fact]
    public void MakeMsg_MergesExtra()
    {
        var msg = NdjsonProtocol.MakeMsg(1, "eval",
            new() { ["command"] = "echo hi", ["timeout"] = 5 });
        Assert.Equal("echo hi", msg["command"]);
        Assert.Equal(5, msg["timeout"]);
    }

    [Fact]
    public void GetChannel_DefaultsToZero()
    {
        var msg = NdjsonProtocol.DecodeFrame("{\"type\":\"ping\"}");
        Assert.Equal(0, NdjsonProtocol.GetChannel(msg));
    }

    [Fact]
    public void GetType_ThrowsWhenMissing()
    {
        var msg = NdjsonProtocol.DecodeFrame("{\"ch\":0}");
        Assert.Throws<ProtocolException>(() => NdjsonProtocol.GetType(msg));
    }

    [Fact]
    public void GetHelpers_ReturnDefaults()
    {
        var msg = NdjsonProtocol.DecodeFrame("{\"ch\":0,\"type\":\"test\"}");
        Assert.Equal("", NdjsonProtocol.GetString(msg, "missing"));
        Assert.Equal("fallback", NdjsonProtocol.GetString(msg, "missing", "fallback"));
        Assert.Equal(0, NdjsonProtocol.GetInt(msg, "missing"));
        Assert.Equal(42, NdjsonProtocol.GetInt(msg, "missing", 42));
        Assert.Equal(0L, NdjsonProtocol.GetLong(msg, "missing"));
        Assert.False(NdjsonProtocol.GetBool(msg, "missing"));
        Assert.Empty(NdjsonProtocol.GetStringList(msg, "missing"));
        Assert.Null(NdjsonProtocol.GetObject(msg, "missing"));
    }

    [Fact]
    public void GetStringList_ParsesArray()
    {
        var msg = NdjsonProtocol.DecodeFrame(
            "{\"ch\":0,\"type\":\"t\",\"attrs\":[\"readonly\",\"export\"]}");
        var attrs = NdjsonProtocol.GetStringList(msg, "attrs");
        Assert.Equal(2, attrs.Count);
        Assert.Equal("readonly", attrs[0]);
        Assert.Equal("export", attrs[1]);
    }

    [Fact]
    public void GetObject_ParsesNestedObject()
    {
        var msg = NdjsonProtocol.DecodeFrame(
            "{\"ch\":0,\"type\":\"t\",\"data\":{\"name\":\"foo\",\"value\":\"bar\"}}");
        var data = NdjsonProtocol.GetObject(msg, "data");
        Assert.NotNull(data);
        Assert.Equal("foo", NdjsonProtocol.GetString(data!, "name"));
        Assert.Equal("bar", NdjsonProtocol.GetString(data!, "value"));
    }
}
