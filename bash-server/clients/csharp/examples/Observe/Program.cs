// Example: subscribe to command observation events.
// Usage: dotnet run -- /tmp/bash-server-1000/sock <token>

using BashServer.Client;

if (args.Length < 2)
{
    Console.Error.WriteLine("usage: observe <socket-path> <token>");
    return 1;
}

string socketPath = args[0];
string token = args[1];

await using var client = await BashClient.ConnectAsync(socketPath);
await client.AuthAsync(token);

client.Observe.PreCommand += (_, e) =>
    Console.WriteLine($"[pre]  seq={e.Seq} cmd={e.Command} cwd={e.Cwd}");
client.Observe.PostCommand += (_, e) =>
    Console.WriteLine($"[post] seq={e.Seq} cmd={e.Command} exit={e.ExitStatus} duration={e.DurationMs}ms");

await client.Observe.SubscribeAsync(level: 1);
Console.WriteLine("Subscribed to observe events. Press Ctrl-C to exit.");

using var cts = new CancellationTokenSource();
Console.CancelKeyPress += (_, e) => { e.Cancel = true; cts.Cancel(); };

try { await Task.Delay(Timeout.Infinite, cts.Token); }
catch (OperationCanceledException) { }

await client.Observe.UnsubscribeAsync();
return 0;
