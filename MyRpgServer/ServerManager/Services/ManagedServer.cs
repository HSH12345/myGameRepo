using System.Collections.Concurrent;
using System.ComponentModel;
using System.Diagnostics;
using System.Net.Sockets;
using System.Net.Http;
using System.Runtime.CompilerServices;
using System.Text;
using System.Windows.Threading;
using RpgServerManager.Models;

namespace RpgServerManager.Services;
public sealed class ManagedServer : INotifyPropertyChanged
{
    private readonly Dispatcher dispatcher;
    private readonly ProcessRegistry processRegistry;
    private readonly int logRetention;
    private readonly int shutdownTimeoutSeconds;
    private Process? process;
    private ServerState state = ServerState.STOPPED;
    private DateTimeOffset? startedAt;
    private string statusDetail = "대기 중";
    private long workingSetBytes;
    private int processId;
    private bool intentionalStop;
    private bool reconnectedProcess;
    private long processGeneration;
    private readonly ConcurrentQueue<LogEntry> pendingLogs = new();
    private readonly DispatcherTimer logFlushTimer;
    public ManagedServer(ServerDefinition definition, int logRetention, int shutdownTimeoutSeconds, Dispatcher dispatcher, ProcessRegistry processRegistry)
    {
        Definition = definition;
        this.logRetention = Math.Max(500, logRetention);
        this.shutdownTimeoutSeconds = Math.Max(1, shutdownTimeoutSeconds);
        this.dispatcher = dispatcher;
        this.processRegistry = processRegistry;
        this.logFlushTimer = new DispatcherTimer(TimeSpan.FromMilliseconds(100), DispatcherPriority.Background, (_, _) => FlushPendingLogs(), dispatcher);
        this.logFlushTimer.Start();
    }

    public ServerDefinition Definition { get; }
    public RangeObservableCollection<LogEntry> Logs { get; } = [];
    public string Id => Definition.Id;
    public string DisplayName => Definition.DisplayName;
    public string Description => Definition.Description;
    public string Group => Definition.Group;
    public int Port => Definition.Port;
    public int StartOrder => Definition.StartOrder;

    public ServerState State
    {
        get => this.state;
        private set
        {
            if (this.state == value)
            {
                return;
            }

            this.state = value;
            OnPropertyChanged();
            OnPropertyChanged(nameof(StateText));
            OnPropertyChanged(nameof(IsRunning));
            OnPropertyChanged(nameof(CanStart));
            OnPropertyChanged(nameof(CanStop));
            OnPropertyChanged(nameof(CanSendCommand));
        }

    }

    public string StateText => State switch
    {
        ServerState.STOPPED => "STOPPED",
        ServerState.STARTING => "STARTING",
        ServerState.RUNNING => "RUNNING",
        ServerState.STOPPING => "STOPPING",
        ServerState.FAILED => "FAILED",
        ServerState.EXTERNAL => "EXTERNAL",
        ServerState.MISCONFIGURED => "CONFIG",
        _ => State.ToString().ToUpperInvariant()};
    public string StatusDetail { get => this.statusDetail; private set => SetField(ref this.statusDetail, value); }
    public int ProcessId { get => this.processId; private set => SetField(ref this.processId, value); }

    public long WorkingSetBytes
    {
        get => this.workingSetBytes;
        private set
        {
            if (SetField(ref this.workingSetBytes, value))
            {
                OnPropertyChanged(nameof(WorkingSetText));
            }

        }

    }

    public string WorkingSetText => WorkingSetBytes <= 0 ? "—" : $"{WorkingSetBytes / 1024d / 1024d:N1} MB";
    public string UptimeText => this.startedAt is null ? "—" : FormatUptime(DateTimeOffset.Now - this.startedAt.Value);
    public bool IsRunning => State is ServerState.RUNNING or ServerState.STARTING or ServerState.STOPPING;
    public bool CanStart => State is ServerState.STOPPED or ServerState.FAILED or ServerState.MISCONFIGURED;
    public bool CanStop => State is ServerState.RUNNING or ServerState.STARTING;
    public bool CanSendCommand => State == ServerState.RUNNING && this.process is { HasExited: false } && !this.reconnectedProcess;
    public bool HasRequiredEnvironment => Definition.RequiredEnvironment.Count > 0;
    public IReadOnlyList<string> MissingEnvironmentVariables => Definition.RequiredEnvironment.Where(name => !Definition.Environment.TryGetValue(name, out var configured) || string.IsNullOrWhiteSpace(configured)).Where(name => string.IsNullOrWhiteSpace(Environment.GetEnvironmentVariable(name))).ToArray();

    public event PropertyChangedEventHandler? PropertyChanged;
    public event Action<ManagedServer, IReadOnlyList<LogEntry>>? LogBatchAdded;
    public async Task StartAsync()
    {
        if (!CanStart)
        {
            return;
        }

        if (Definition.Port > 0 && await IsPortOpenAsync(Definition.Port))
        {
            State = ServerState.EXTERNAL;
            StatusDetail = $"포트 {Definition.Port}가 다른 프로세스에서 사용 중";
            AddManagerLog("Warning", StatusDetail);
            return;
        }

        var missing = MissingEnvironmentVariables;
        if (missing.Count > 0)
        {
            State = ServerState.MISCONFIGURED;
            StatusDetail = $"필수 환경 변수 누락: {string.Join(", ", missing)}";
            AddManagerLog("Error", StatusDetail);
            return;
        }

        if (Definition.Executable.Contains(Path.DirectorySeparatorChar) && !File.Exists(Definition.Executable))
        {
            State = ServerState.MISCONFIGURED;
            StatusDetail = $"실행 파일 없음: {Definition.Executable}";
            AddManagerLog("Error", StatusDetail);
            return;
        }

        if (!Directory.Exists(Definition.WorkingDirectory))
        {
            State = ServerState.MISCONFIGURED;
            StatusDetail = $"작업 폴더 없음: {Definition.WorkingDirectory}";
            AddManagerLog("Error", StatusDetail);
            return;
        }

        State = ServerState.STARTING;
        StatusDetail = "프로세스 시작 중";
        this.intentionalStop = false;
        this.reconnectedProcess = false;
        try
        {
            var startInfo = new ProcessStartInfo
            {
                FileName = Definition.Executable,
                Arguments = Definition.Arguments,
                WorkingDirectory = Definition.WorkingDirectory,
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                RedirectStandardInput = true,
                StandardOutputEncoding = Encoding.UTF8,
                StandardErrorEncoding = Encoding.UTF8
            };

            foreach (var pair in Definition.Environment)
            {
                startInfo.Environment[pair.Key] = pair.Value;
            }

            var launchedProcess = new Process
            {
                StartInfo = startInfo,
                EnableRaisingEvents = true
            };

            var generation = ++this.processGeneration;
            this.process = launchedProcess;
            launchedProcess.OutputDataReceived += (_, args) => OnOutput(args.Data, false);
            launchedProcess.ErrorDataReceived += (_, args) => OnOutput(args.Data, true);
            launchedProcess.Exited += (_, _) => this.dispatcher.BeginInvoke(() => HandleProcessExit(launchedProcess, generation));
            if (!launchedProcess.Start())
            {
                throw new InvalidOperationException("Process.Start returned false.");
            }

            launchedProcess.BeginOutputReadLine();
            launchedProcess.BeginErrorReadLine();
            ProcessId = launchedProcess.Id;
            this.startedAt = DateTimeOffset.Now;
            this.processRegistry.Save(new ManagedProcessRecord { ServerId = Id, ProcessId = ProcessId, StartTimeUtc = launchedProcess.StartTime.ToUniversalTime(), ExecutableName = launchedProcess.ProcessName });
            if (Definition.Port > 0 && !await WaitForPortAsync(Definition.Port, TimeSpan.FromSeconds(10)))
            {
                throw new TimeoutException($"프로세스가 시작됐지만 포트 {Definition.Port}가 준비되지 않았습니다.");
            }

            State = ServerState.RUNNING;
            StatusDetail = Definition.Port > 0 ? $"포트 {Definition.Port} · PID {ProcessId}" : $"PID {ProcessId}";
            AddManagerLog("Info", $"프로세스를 시작했습니다. PID={ProcessId}");
        }

        catch (Exception exception)
        {
            var failedProcess = this.process;
            try
            {
                if (failedProcess is { HasExited: false })
                {
                    failedProcess.Kill(true);
                }

            }

            catch
            {
            }

            failedProcess?.Dispose();
            if (ReferenceEquals(this.process, failedProcess))
            {
                this.process = null;
            }

            this.processRegistry.Remove(Id);
            ProcessId = 0;
            State = ServerState.FAILED;
            StatusDetail = exception.Message;
            AddManagerLog("Error", $"시작 실패: {exception.Message}");
        }

    }

    public async Task StopAsync()
    {
        var stoppingProcess = this.process;
        if (!CanStop || stoppingProcess is null)
        {
            return;
        }

        State = ServerState.STOPPING;
        StatusDetail = "정상 종료 요청 중";
        this.intentionalStop = true;
        AddManagerLog("Info", "정상 종료를 요청했습니다.");
        try
        {
            if (this.reconnectedProcess && !string.IsNullOrWhiteSpace(Definition.ShutdownUrl))
            {
                using var client = new HttpClient
                {
                    Timeout = TimeSpan.FromSeconds(this.shutdownTimeoutSeconds)
                };

                using var response = await client.PostAsync(Definition.ShutdownUrl, null);
                response.EnsureSuccessStatusCode();
            }
            else if (!this.reconnectedProcess && !string.IsNullOrWhiteSpace(Definition.ShutdownCommand))
            {
                await stoppingProcess.StandardInput.WriteLineAsync(Definition.ShutdownCommand);
                await stoppingProcess.StandardInput.FlushAsync();
            }
            else if (this.reconnectedProcess)
            {
                AddManagerLog("Warning", "재연결된 프로세스는 표준 입력을 복구할 수 없어 프로세스 트리를 종료합니다.");
                stoppingProcess.Kill(true);
            }

            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(this.shutdownTimeoutSeconds));
            await stoppingProcess.WaitForExitAsync(timeout.Token);
        }

        catch (OperationCanceledException)
        {
            AddManagerLog("Warning", "정상 종료 시간이 초과되어 프로세스 트리를 종료합니다.");
            if (!stoppingProcess.HasExited)
            {
                stoppingProcess.Kill(true);
            }

        }

        catch (Exception exception)
        {
            AddManagerLog("Error", $"종료 요청 실패: {exception.Message}");
            if (!stoppingProcess.HasExited)
            {
                stoppingProcess.Kill(true);
            }

        }

    }

    public async Task RestartAsync()
    {
        if (CanStop)
        {
            await StopAsync();
        }

        await Task.Delay(250);
        await StartAsync();
    }

    public void SetEnvironmentVariable(string name, string value) => Definition.Environment[name] = value;
    public async Task SendCommandAsync(string command)
    {
        command = command.Trim();
        if (command.Length == 0)
        {
            return;
        }

        if (!CanSendCommand || this.process is null)
        {
            throw new InvalidOperationException(this.reconnectedProcess ? "재연결된 프로세스의 기존 stdin 채널은 복구할 수 없습니다. 서버를 재시작한 뒤 명령을 보내주세요." : "실행 중이며 현재 관리자가 시작한 서버에만 명령을 보낼 수 있습니다.");
        }

        await this.process.StandardInput.WriteLineAsync(command);
        await this.process.StandardInput.FlushAsync();
        AddManagerLog("Info", $"> {command}");
    }

    public async Task<bool> TryReconnectAsync()
    {
        var record = this.processRegistry.Find(Id);
        if (record is null)
        {
            return false;
        }

        try
        {
            var existing = Process.GetProcessById(record.ProcessId);
            if (existing.HasExited || Math.Abs((existing.StartTime.ToUniversalTime() - record.StartTimeUtc).TotalSeconds) > 2 || !string.Equals(existing.ProcessName, record.ExecutableName, StringComparison.OrdinalIgnoreCase) || (Definition.Port > 0 && !await IsPortOpenAsync(Definition.Port)))
            {
                existing.Dispose();
                this.processRegistry.Remove(Id);
                return false;
            }

            var generation = ++this.processGeneration;
            this.process = existing;
            existing.EnableRaisingEvents = true;
            existing.Exited += (_, _) => this.dispatcher.BeginInvoke(() => HandleProcessExit(existing, generation));
            ProcessId = existing.Id;
            this.startedAt = existing.StartTime;
            this.reconnectedProcess = true;
            this.intentionalStop = false;
            State = ServerState.RUNNING;
            OnPropertyChanged(nameof(CanSendCommand));
            StatusDetail = Definition.Port > 0 ? $"재연결됨 · 포트 {Definition.Port} · PID {ProcessId}" : $"재연결됨 · PID {ProcessId}";
            AddManagerLog("Info", $"이전 관리 세션의 프로세스를 인계받았습니다. PID={ProcessId}");
            return true;
        }

        catch (ArgumentException)
        {
            this.processRegistry.Remove(Id);
            return false;
        }

        catch (InvalidOperationException)
        {
            this.processRegistry.Remove(Id);
            return false;
        }

    }

    public async Task RefreshAsync()
    {
        if (this.process is { HasExited: false })
        {
            try
            {
                this.process.Refresh();
                WorkingSetBytes = this.process.WorkingSet64;
                OnPropertyChanged(nameof(UptimeText));
            }

            catch (InvalidOperationException)
            {
            }

            return;
        }

        WorkingSetBytes = 0;
        OnPropertyChanged(nameof(UptimeText));
        if (Definition.Port > 0 && State is ServerState.STOPPED or ServerState.EXTERNAL or ServerState.FAILED)
        {
            var external = await IsPortOpenAsync(Definition.Port);
            if (external && State != ServerState.EXTERNAL)
            {
                State = ServerState.EXTERNAL;
                StatusDetail = $"외부 프로세스가 포트 {Definition.Port} 사용 중";
            }
            else if (!external && State == ServerState.EXTERNAL)
            {
                State = ServerState.STOPPED;
                StatusDetail = "대기 중";
            }

        }

    }

    public void ClearLogs()
    {
        while (this.pendingLogs.TryDequeue(out _))
        {
        }

        Logs.Clear();
    }

    private void OnOutput(string? line, bool isErrorStream)
    {
        if (string.IsNullOrWhiteSpace(line))
        {
            return;
        }

        var entry = LogParser.Parse(Id, DisplayName, line, isErrorStream);
        this.pendingLogs.Enqueue(entry);
    }

    private void AddManagerLog(string level, string message) => AddLog(LogParser.Manager(Id, DisplayName, level, message));
    private void AddLog(LogEntry entry)
    {
        Logs.Add(entry);
        Logs.TrimStart(Logs.Count - this.logRetention);
        LogBatchAdded?.Invoke(this, [entry]);
    }

    private void FlushPendingLogs()
    {
        const int SIZE = 1000;
        var count = 0;
        var batch = new List<LogEntry>();
        while (count++ < SIZE && this.pendingLogs.TryDequeue(out var entry))
        {
            Logs.Add(entry);
            batch.Add(entry);
        }

        Logs.TrimStart(Logs.Count - this.logRetention);
        if (batch.Count > 0)
        {
            LogBatchAdded?.Invoke(this, batch);
        }

    }

    private void HandleProcessExit(Process exitedProcess, long generation)
    {
        if (generation != this.processGeneration || !ReferenceEquals(this.process, exitedProcess))
        {
            exitedProcess.Dispose();
            return;
        }

        var exitCode = 0;
        try
        {
            exitCode = exitedProcess.ExitCode;
        }

        catch (InvalidOperationException)
        {
        }

        exitedProcess.Dispose();
        this.process = null;
        this.processRegistry.Remove(Id);
        ProcessId = 0;
        WorkingSetBytes = 0;
        this.startedAt = null;
        State = this.intentionalStop || exitCode == 0 ? ServerState.STOPPED : ServerState.FAILED;
        StatusDetail = this.intentionalStop ? "정상 종료됨" : $"프로세스 종료 · 코드 {exitCode}";
        AddManagerLog(State == ServerState.FAILED ? "Error" : "Info", StatusDetail);
        this.intentionalStop = false;
        this.reconnectedProcess = false;
        OnPropertyChanged(nameof(CanSendCommand));
    }

    private static async Task<bool> IsPortOpenAsync(int port)
    {
        try
        {
            using var client = new TcpClient();
            using var timeout = new CancellationTokenSource(TimeSpan.FromMilliseconds(180));
            await client.ConnectAsync("127.0.0.1", port, timeout.Token);
            return true;
        }

        catch
        {
            return false;
        }

    }

    private async Task<bool> WaitForPortAsync(int port, TimeSpan timeout)
    {
        var deadline = DateTimeOffset.UtcNow + timeout;
        while (DateTimeOffset.UtcNow < deadline)
        {
            if (this.process is null || this.process.HasExited)
            {
                return false;
            }

            if (await IsPortOpenAsync(port))
            {
                return true;
            }

            await Task.Delay(100);
        }

        return false;
    }

    private static string FormatUptime(TimeSpan value) => value.TotalDays >= 1 ? $"{(int)value.TotalDays}d {value:hh\\:mm\\:ss}" : value.ToString("hh\\:mm\\:ss");
    private bool SetField<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value))
        {
            return false;
        }

        field = value;
        OnPropertyChanged(propertyName);
        return true;
    }

    private void OnPropertyChanged([CallerMemberName] string? propertyName = null) => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
}

