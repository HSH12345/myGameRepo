namespace RpgServerManager.Models;
public sealed class ServerConfiguration
{
    public int LogRetention { get; set; } = 20_000;
    public int ShutdownTimeoutSeconds { get; set; } = 5;
    public List<ServerDefinition> Servers { get; set; } = [];
}

public sealed class ServerDefinition
{
    public string Id { get; set; } = string.Empty;
    public string DisplayName { get; set; } = string.Empty;
    public string Description { get; set; } = string.Empty;
    public string Group { get; set; } = "Native";
    public string Executable { get; set; } = string.Empty;
    public string Arguments { get; set; } = string.Empty;
    public string WorkingDirectory { get; set; } = string.Empty;
    public string ShutdownCommand { get; set; } = "-shutdown";
    public string ShutdownUrl { get; set; } = string.Empty;
    public int Port { get; set; }
    public int StartOrder { get; set; } = 100;
    public bool AutoStart { get; set; }
    public Dictionary<string, string> Environment { get; set; } = new(StringComparer.OrdinalIgnoreCase);
    public List<string> RequiredEnvironment { get; set; } = [];
}

