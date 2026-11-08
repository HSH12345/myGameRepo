using System.Text.Json;
using RpgServerManager.Models;

namespace RpgServerManager.Services;
public static class LogParser
{
    private const string PREFIX = "@rpglog ";
    public static LogEntry Parse(string serverId, string serverName, string line, bool isErrorStream)
    {
        if (line.StartsWith(PREFIX, StringComparison.Ordinal))
        {
            try
            {
                using var document = JsonDocument.Parse(line[PREFIX.Length..]);
                var root = document.RootElement;
                return new LogEntry
                {
                    Timestamp = TryGetTimestamp(root),
                    ServerId = serverId,
                    ServerName = serverName,
                    Level = GetString(root, "level", isErrorStream ? "Error" : "Info"),
                    Category = GetString(root, "category", "General"),
                    Message = GetString(root, "message", line),
                    Thread = GetString(root, "thread", string.Empty)
                };

            }

            catch (JsonException)
            {
            // Preserve malformed structured output as a normal console line.
            }

        }

        return new LogEntry
        {
            Timestamp = DateTimeOffset.Now,
            ServerId = serverId,
            ServerName = serverName,
            Level = isErrorStream ? "Error" : "Info",
            Category = "Console",
            Message = line
        };

    }

    public static LogEntry Manager(string serverId, string serverName, string level, string message) => new()
    {
        Timestamp = DateTimeOffset.Now,
        ServerId = serverId,
        ServerName = serverName,
        Level = level,
        Category = "Manager",
        Message = message
    };

    private static DateTimeOffset TryGetTimestamp(JsonElement root)
    {
        var text = GetString(root, "timestamp", string.Empty);
        return DateTimeOffset.TryParse(text, out var parsed) ? parsed : DateTimeOffset.Now;
    }

    private static string GetString(JsonElement root, string propertyName, string fallback)
    {
        if (root.TryGetProperty(propertyName, out var value) && value.ValueKind == JsonValueKind.String)
        {
            return value.GetString() ?? fallback;
        }

        return fallback;
    }

}

