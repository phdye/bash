// Example: set a breakpoint and step through execution.
// Usage: dotnet run -- /tmp/bash-server-1000/sock <token>

using BashServer.Client;

if (args.Length < 2)
{
    Console.Error.WriteLine("usage: debugger <socket-path> <token>");
    return 1;
}

string socketPath = args[0];
string token = args[1];

await using var client = await BashClient.ConnectAsync(socketPath);
await client.AuthAsync(token);

client.Debug.BreakHit += (_, e) =>
    Console.WriteLine($"[break] line={e.Line} cmd={e.Command} depth={e.Depth}");

await client.Debug.EnableAsync();
Console.WriteLine("Debugger enabled.");

var bpId = await client.Debug.AddBreakpointAsync("command", pattern: "echo");
Console.WriteLine($"Added breakpoint id={bpId} on 'echo' commands.");

var status = await client.Debug.StatusAsync();
Console.WriteLine($"Status: active={status.Active} mode={status.Mode} breakpoints={status.Breakpoints}");

var breakpoints = await client.Debug.ListBreakpointsAsync();
foreach (var bp in breakpoints)
    Console.WriteLine($"  bp id={bp.Id} type={bp.Kind} enabled={bp.Enabled}");

Console.WriteLine("Run 'echo hello' in the bash-server session to hit the breakpoint.");
Console.WriteLine("Press Ctrl-C to exit.");

using var cts = new CancellationTokenSource();
Console.CancelKeyPress += (_, e) => { e.Cancel = true; cts.Cancel(); };

try { await Task.Delay(Timeout.Infinite, cts.Token); }
catch (OperationCanceledException) { }

await client.Debug.DisableAsync();
return 0;
