// Example: evaluate a command via bash-server.
// Usage: dotnet run -- /tmp/bash-server-1000/sock <token> [command]

using BashServer.Client;

if (args.Length < 2)
{
    Console.Error.WriteLine("usage: eval <socket-path> <token> [command]");
    return 1;
}

string socketPath = args[0];
string token = args[1];
string command = args.Length > 2 ? args[2] : "echo 'Hello from bash-server'";

await using var client = await BashClient.ConnectAsync(socketPath);
await client.AuthAsync(token);

var result = await client.EvalAsync(command);
if (!string.IsNullOrEmpty(result.Stdout))
    Console.Write(result.Stdout);
if (!string.IsNullOrEmpty(result.Stderr))
    Console.Error.Write(result.Stderr);

return result.ExitCode;
