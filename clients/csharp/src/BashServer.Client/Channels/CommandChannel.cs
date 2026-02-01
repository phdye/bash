using Msg = System.Collections.Generic.Dictionary<string, System.Text.Json.JsonElement>;

namespace BashServer.Client.Channels;

/// <summary>Channel 1: command evaluation.</summary>
public class CommandChannel
{
    private readonly Func<Dictionary<string, object?>, Task> _send;
    private readonly Func<int, CancellationToken, Task<Msg>> _recv;

    internal CommandChannel(
        Func<Dictionary<string, object?>, Task> send,
        Func<int, CancellationToken, Task<Msg>> recv)
    {
        _send = send;
        _recv = recv;
    }

    public async Task<EvalResult> EvalAsync(string command, CancellationToken ct = default)
    {
        await _send(NdjsonProtocol.MakeMsg(Ch.Command, "eval",
            new() { ["command"] = command })).ConfigureAwait(false);

        string stdout = "", stderr = "";
        int exitCode = -1;

        for (int i = 0; i < 3; i++)
        {
            var resp = await _recv(Ch.Command, ct).ConfigureAwait(false);
            var t = NdjsonProtocol.GetType(resp);
            switch (t)
            {
                case "stdout":
                    var sData = NdjsonProtocol.GetString(resp, "data");
                    if (NdjsonProtocol.GetString(resp, "encoding") == "base64")
                        sData = NdjsonProtocol.B64Decode(sData);
                    stdout = sData;
                    break;
                case "stderr":
                    var eData = NdjsonProtocol.GetString(resp, "data");
                    if (NdjsonProtocol.GetString(resp, "encoding") == "base64")
                        eData = NdjsonProtocol.B64Decode(eData);
                    stderr = eData;
                    break;
                case "complete":
                    exitCode = NdjsonProtocol.GetInt(resp, "exit_code", -1);
                    break;
                case "error":
                    throw new ServerException(
                        NdjsonProtocol.GetString(resp, "message", "eval error"),
                        Ch.Command);
            }
        }

        return new EvalResult(stdout, stderr, exitCode);
    }
}
