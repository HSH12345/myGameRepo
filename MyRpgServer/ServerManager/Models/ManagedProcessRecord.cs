namespace RpgServerManager.Models;
public sealed class ManagedProcessRecord
{
    public string ServerId { get; set; } = string.Empty;
    public int ProcessId { get; set; }
    public DateTime StartTimeUtc { get; set; }
    public string ExecutableName { get; set; } = string.Empty;
}

