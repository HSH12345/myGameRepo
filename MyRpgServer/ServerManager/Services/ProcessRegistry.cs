using System.Text.Json;
using RpgServerManager.Models;

namespace RpgServerManager.Services;
public sealed class ProcessRegistry
{
    private readonly object sync = new();
    private readonly string registryPath;
    private readonly JsonSerializerOptions jsonOptions = new()
    {
        WriteIndented = true
    };

    public ProcessRegistry()
    {
        var directory = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "RpgServerManager");
        Directory.CreateDirectory(directory);
        this.registryPath = Path.Combine(directory, "managed-processes.json");
    }

    public ManagedProcessRecord? Find(string serverId)
    {
        lock (this.sync)
        {
            return Load().FirstOrDefault(record => string.Equals(record.ServerId, serverId, StringComparison.OrdinalIgnoreCase));
        }

    }

    public void Save(ManagedProcessRecord record)
    {
        lock (this.sync)
        {
            var records = Load();
            records.RemoveAll(item => string.Equals(item.ServerId, record.ServerId, StringComparison.OrdinalIgnoreCase));
            records.Add(record);
            Write(records);
        }

    }

    public void Remove(string serverId)
    {
        lock (this.sync)
        {
            var records = Load();
            if (records.RemoveAll(item => string.Equals(item.ServerId, serverId, StringComparison.OrdinalIgnoreCase)) > 0)
            {
                Write(records);
            }

        }

    }

    private List<ManagedProcessRecord> Load()
    {
        try
        {
            if (!File.Exists(this.registryPath))
            {
                return[];
            }

            return JsonSerializer.Deserialize<List<ManagedProcessRecord>>(File.ReadAllText(this.registryPath)) ?? [];
        }

        catch
        {
            return[];
        }

    }

    private void Write(List<ManagedProcessRecord> records)
    {
        var temporaryPath = this.registryPath + ".tmp";
        File.WriteAllText(temporaryPath, JsonSerializer.Serialize(records, this.jsonOptions));
        File.Move(temporaryPath, this.registryPath, true);
    }

}

