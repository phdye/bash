using BashServer.Client;
using Xunit;

namespace BashServer.Client.Tests;

/// <summary>
/// Integration tests that require a running bash-server.
/// Skipped when bash-server is not available.
/// </summary>
[Trait("Category", "Integration")]
public class IntegrationTests
{
    private static bool HasBashServer()
    {
        try
        {
            var psi = new System.Diagnostics.ProcessStartInfo("bash-server", "--version")
            {
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false,
                CreateNoWindow = true,
            };
            using var proc = System.Diagnostics.Process.Start(psi);
            proc?.WaitForExit(5000);
            return proc?.ExitCode == 0;
        }
        catch
        {
            return false;
        }
    }

    [Fact]
    public async Task StdioTransport_AuthAndEval()
    {
        if (!HasBashServer())
        {
            // Skip: bash-server not found in PATH
            return;
        }

        await using var client = await BashClient.ConnectStdioAsync("bash-server", "--stdio");
        // stdio mode does not require auth token
        var result = await client.EvalAsync("echo integration-test");
        Assert.Contains("integration-test", result.Stdout);
        Assert.Equal(0, result.ExitCode);
    }

    [Fact]
    public async Task StdioTransport_Ping()
    {
        if (!HasBashServer())
            return;

        await using var client = await BashClient.ConnectStdioAsync("bash-server", "--stdio");
        await client.PingAsync();
    }

    [Fact]
    public async Task StdioTransport_StateGetVar()
    {
        if (!HasBashServer())
            return;

        await using var client = await BashClient.ConnectStdioAsync("bash-server", "--stdio");
        var info = await client.State.GetVarAsync("HOME");
        Assert.Equal("HOME", info.Name);
        Assert.NotEmpty(info.Value);
    }
}
