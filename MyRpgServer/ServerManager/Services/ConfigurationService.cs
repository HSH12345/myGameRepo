using System.Text.Json;
using RpgServerManager.Models;

namespace RpgServerManager.Services;
public sealed class ConfigurationService
{
    private static readonly JsonSerializerOptions jsonOptions = new()
    {
        PropertyNameCaseInsensitive = true,
        ReadCommentHandling = JsonCommentHandling.Skip,
        AllowTrailingCommas = true,
        WriteIndented = true
    };

    public string RepositoryRoot { get; }
    public string WebRoot { get; }
    public string ConfigurationPath { get; }

    public ConfigurationService()
    {
        RepositoryRoot = FindRepositoryRoot();
        WebRoot = Path.GetFullPath(Path.Combine(RepositoryRoot, "..", "Web"));
        ConfigurationPath = ResolveConfigurationPath();
    }

    public async Task<ServerConfiguration> LoadAsync()
    {
        if (!File.Exists(ConfigurationPath))
        {
            throw new FileNotFoundException("Server manager configuration was not found.", ConfigurationPath);
        }

        await using var stream = File.OpenRead(ConfigurationPath);
        var configuration = await JsonSerializer.DeserializeAsync<ServerConfiguration>(stream, jsonOptions) ?? throw new InvalidDataException("Server manager configuration is empty.");
        var duplicate = configuration.Servers.GroupBy(server => server.Id, StringComparer.OrdinalIgnoreCase).FirstOrDefault(group => string.IsNullOrWhiteSpace(group.Key) || group.Count() > 1);
        if (duplicate is not null)
        {
            throw new InvalidDataException($"Server id must be non-empty and unique: '{duplicate.Key}'.");
        }

        foreach (var definition in configuration.Servers)
        {
            definition.Executable = ExpandPath(definition.Executable);
            definition.WorkingDirectory = ExpandPath(definition.WorkingDirectory);
            definition.Arguments = ExpandTokens(definition.Arguments);
            definition.Environment = definition.Environment.ToDictionary(pair => pair.Key, pair => ExpandEnvironment(pair.Value), StringComparer.OrdinalIgnoreCase);
        }

        return configuration;
    }

    private string ResolveConfigurationPath()
    {
        var environmentPath = Environment.GetEnvironmentVariable("RPG_SERVER_MANAGER_CONFIG");
        if (!string.IsNullOrWhiteSpace(environmentPath))
        {
            return Path.GetFullPath(environmentPath);
        }

        var besideExecutable = Path.Combine(AppContext.BaseDirectory, "servers.json");
        if (File.Exists(besideExecutable))
        {
            return besideExecutable;
        }

        return Path.Combine(RepositoryRoot, "ServerManager", "servers.json");
    }

    private string ExpandPath(string value)
    {
        var expanded = ExpandTokens(value);
        if (string.IsNullOrWhiteSpace(expanded))
        {
            return RepositoryRoot;
        }

        if (!Path.IsPathRooted(expanded) && !expanded.Contains(Path.DirectorySeparatorChar) && !expanded.Contains(Path.AltDirectorySeparatorChar))
        {
            return expanded;
        }

        if (!Path.IsPathRooted(expanded))
        {
            expanded = Path.Combine(RepositoryRoot, expanded);
        }

        return Path.GetFullPath(expanded);
    }

    private string ExpandTokens(string value) => value.Replace("{RepoRoot}", RepositoryRoot, StringComparison.OrdinalIgnoreCase).Replace("{WebRoot}", WebRoot, StringComparison.OrdinalIgnoreCase);
    private string ExpandEnvironment(string value)
    {
        var expanded = ExpandTokens(value);
        foreach (System.Collections.DictionaryEntry item in Environment.GetEnvironmentVariables())
        {
            expanded = expanded.Replace($"${{{item.Key}}}", item.Value?.ToString() ?? string.Empty, StringComparison.OrdinalIgnoreCase);
        }

        return expanded;
    }

    private static string FindRepositoryRoot()
    {
        var candidates = new[]
        {
            AppContext.BaseDirectory,
            Environment.CurrentDirectory
        };

        foreach (var candidate in candidates)
        {
            var directory = new DirectoryInfo(candidate);
            while (directory is not null)
            {
                if (File.Exists(Path.Combine(directory.FullName, "MyRpgServer.sln")))
                {
                    return directory.FullName;
                }

                directory = directory.Parent;
            }

        }

        throw new DirectoryNotFoundException("Could not locate MyRpgServer.sln from the manager executable.");
    }

}

