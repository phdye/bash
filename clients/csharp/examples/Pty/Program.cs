// Example: spawn a PTY and interact with it.
// Usage: dotnet run -- /tmp/bash-server-1000/sock <token>

using BashServer.Client;

if (args.Length < 2)
{
    Console.Error.WriteLine("usage: pty <socket-path> <token>");
    return 1;
}

string socketPath = args[0];
string token = args[1];

await using var client = await BashClient.ConnectAsync(socketPath);
await client.AuthAsync(token);

client.Pty.Output += (_, data) => Console.Write(data);
client.Pty.Exit += (_, code) => Console.WriteLine($"\n[PTY exited with code {code}]");

var info = await client.Pty.SpawnAsync(new PtyOpts
{
    Rows = 24,
    Cols = 80,
    StripAnsi = false,
});
Console.WriteLine($"PTY spawned: pid={info.Pid} {info.Cols}x{info.Rows}");

// Send a command and wait for output
await client.Pty.WriteInputAsync("echo 'Hello from PTY'\n");
await Task.Delay(1000);

await client.Pty.WriteInputAsync("exit\n");
await Task.Delay(500);

return 0;
