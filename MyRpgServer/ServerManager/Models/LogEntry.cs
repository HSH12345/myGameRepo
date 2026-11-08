namespace RpgServerManager.Models;
public sealed class LogEntry
{
    public DateTimeOffset Timestamp { get; init; } = DateTimeOffset.Now;
    public string ServerId { get; init; } = string.Empty;
    public string ServerName { get; init; } = string.Empty;
    public string Level { get; init; } = "Info";
    public string Category { get; init; } = "Console";
    public string Message { get; init; } = string.Empty;
    public string Thread { get; init; } = string.Empty;
    public string TimestampText => Timestamp.ToLocalTime().ToString("HH:mm:ss.fff");
}

