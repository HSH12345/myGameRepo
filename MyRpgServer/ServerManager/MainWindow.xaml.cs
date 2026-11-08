using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Text.Json;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Data;
using System.Windows.Input;
using System.Windows.Threading;
using Microsoft.Win32;
using RpgServerManager.Models;
using RpgServerManager.Services;

namespace RpgServerManager;
public partial class MainWindow : Window, INotifyPropertyChanged
{
    private readonly ConfigurationService configurationService = new();
    private readonly ProcessRegistry processRegistry = new();
    private readonly CredentialStore credentialStore = new();
    private readonly DispatcherTimer refreshTimer;
    private readonly HashSet<string> enabledLevels = new(StringComparer.OrdinalIgnoreCase)
    {
        "Trace",
        "Debug",
        "Info",
        "Warning",
        "Error",
        "Critical"
    };

    private readonly List<string> commandHistory = [];
    private int commandHistoryIndex;
    private ManagedServer? selectedServer;
    private ICollectionView? filteredLogs;
    private string footerStatus = "설정을 읽는 중입니다.";
    private int visibleLogCount;
    private bool closeAfterShutdown;
    public MainWindow()
    {
        InitializeComponent();
        DataContext = this;
        this.refreshTimer = new DispatcherTimer(TimeSpan.FromSeconds(1), DispatcherPriority.Background, RefreshTimer_Tick, Dispatcher);
    }

    public ObservableCollection<ManagedServer> Servers { get; } = [];
    public ObservableCollection<string> Categories { get; } = ["전체 카테고리"];

    public ManagedServer? SelectedServer
    {
        get => this.selectedServer;
        set
        {
            if (this.selectedServer == value)
            {
                return;
            }

            this.selectedServer = value;
            OnPropertyChanged();
            BindSelectedLogs();
        }

    }

    public ICollectionView? FilteredLogs
    {
        get => this.filteredLogs;
        private set
        {
            this.filteredLogs = value;
            OnPropertyChanged();
        }

    }

    public int ServerCount => Servers.Count;
    public int OnlineCount => Servers.Count(server => server.State == ServerState.RUNNING);

    public int VisibleLogCount
    {
        get => this.visibleLogCount;
        private set
        {
            this.visibleLogCount = value;
            OnPropertyChanged();
        }

    }

    public string FooterStatus
    {
        get => this.footerStatus;
        private set
        {
            this.footerStatus = value;
            OnPropertyChanged();
        }

    }

    public event PropertyChangedEventHandler? PropertyChanged;
    private async void Window_Loaded(object sender, RoutedEventArgs e)
    {
        await LoadConfigurationAsync();
        this.refreshTimer.Start();
    }

    private async Task LoadConfigurationAsync()
    {
        try
        {
            var configuration = await this.configurationService.LoadAsync();
            Servers.Clear();
            foreach (var definition in configuration.Servers.OrderBy(server => server.StartOrder))
            {
                var runtime = new ManagedServer(definition, configuration.LogRetention, configuration.ShutdownTimeoutSeconds, Dispatcher, this.processRegistry);
                runtime.PropertyChanged += Runtime_PropertyChanged;
                runtime.LogBatchAdded += Runtime_LogBatchAdded;
                foreach (var variableName in definition.RequiredEnvironment)
                {
                    var storedValue = this.credentialStore.Read(variableName);
                    if (!string.IsNullOrWhiteSpace(storedValue))
                    {
                        runtime.SetEnvironmentVariable(variableName, storedValue);
                    }

                }

                Servers.Add(runtime);
                await runtime.TryReconnectAsync();
            }

            SelectedServer = Servers.FirstOrDefault();
            FooterStatus = $"설정 로드 완료 · {this.configurationService.ConfigurationPath}";
            RaiseSummary();
            foreach (var server in Servers.Where(server => server.Definition.AutoStart))
            {
                await StartServerAsync(server);
            }

        }

        catch (Exception exception)
        {
            FooterStatus = $"설정 오류: {exception.Message}";
            MessageBox.Show(this, exception.Message, "Server Manager 설정 오류", MessageBoxButton.OK, MessageBoxImage.Error);
        }

    }

    private void BindSelectedLogs()
    {
        Categories.Clear();
        Categories.Add("전체 카테고리");
        if (SelectedServer is null)
        {
            FilteredLogs = null;
            VisibleLogCount = 0;
            return;
        }

        foreach (var category in SelectedServer.Logs.Select(log => log.Category).Distinct().OrderBy(value => value))
        {
            Categories.Add(category);
        }

        FilteredLogs = CollectionViewSource.GetDefaultView(SelectedServer.Logs);
        FilteredLogs.Filter = FilterLog;
        CategoryBox.SelectedIndex = 0;
        RefreshLogView();
    }

    private bool FilterLog(object item)
    {
        if (item is not LogEntry log || !this.enabledLevels.Contains(log.Level))
        {
            return false;
        }

        if (CategoryBox.SelectedItem is string category && category != "전체 카테고리" && !string.Equals(category, log.Category, StringComparison.OrdinalIgnoreCase))
        {
            return false;
        }

        var search = SearchBox.Text.Trim();
        return search.Length == 0 || log.Message.Contains(search, StringComparison.OrdinalIgnoreCase) || log.Category.Contains(search, StringComparison.OrdinalIgnoreCase);
    }

    private void Runtime_LogBatchAdded(ManagedServer sender, IReadOnlyList<LogEntry> entries)
    {
        if (sender != SelectedServer)
        {
            return;
        }

        foreach (var category in entries.Select(entry => entry.Category).Distinct())
        {
            if (!Categories.Contains(category))
            {
                Categories.Add(category);
            }

        }

        RefreshLogView();
        var lastVisible = entries.LastOrDefault(FilterLog);
        if (AutoScrollBox.IsChecked == true && lastVisible is not null)
        {
            LogList.ScrollIntoView(lastVisible);
        }

    }

    private void Runtime_PropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName is nameof(ManagedServer.State) or nameof(ManagedServer.StateText))
        {
            RaiseSummary();
        }

    }

    private async void RefreshTimer_Tick(object? sender, EventArgs e)
    {
        foreach (var server in Servers)
        {
            await server.RefreshAsync();
        }

        RaiseSummary();
    }

    private async void StartSelected_Click(object sender, RoutedEventArgs e)
    {
        if (SelectedServer is not null)
        {
            await StartServerAsync(SelectedServer);
        }

    }

    private async void StopSelected_Click(object sender, RoutedEventArgs e)
    {
        if (SelectedServer is not null)
        {
            await SelectedServer.StopAsync();
        }

    }

    private async void RestartSelected_Click(object sender, RoutedEventArgs e)
    {
        if (SelectedServer is null)
        {
            return;
        }

        if (SelectedServer.CanStop)
        {
            await SelectedServer.StopAsync();
        }

        await Task.Delay(250);
        await StartServerAsync(SelectedServer);
    }

    private async void StartAll_Click(object sender, RoutedEventArgs e)
    {
        FooterStatus = "전체 서버를 순서대로 시작하는 중입니다.";
        foreach (var server in Servers.OrderBy(server => server.StartOrder))
        {
            await StartServerAsync(server);
        }

        FooterStatus = "전체 시작 요청이 완료되었습니다.";
    }

    private async Task StartServerAsync(ManagedServer server)
    {
        foreach (var variableName in server.MissingEnvironmentVariables.ToArray())
        {
            if (!PromptAndStoreCredential(server, variableName))
            {
                return;
            }

        }

        await server.StartAsync();
    }

    private void EditCredential_Click(object sender, RoutedEventArgs e)
    {
        if (SelectedServer is null || SelectedServer.Definition.RequiredEnvironment.Count == 0)
        {
            return;
        }

        foreach (var variableName in SelectedServer.Definition.RequiredEnvironment)
        {
            if (!PromptAndStoreCredential(SelectedServer, variableName))
            {
                return;
            }

        }

        FooterStatus = $"{SelectedServer.DisplayName} 보안 값을 갱신했습니다. 다음 시작부터 적용됩니다.";
    }

    private bool PromptAndStoreCredential(ManagedServer server, string variableName)
    {
        var prompt = new SecretPromptWindow(server.DisplayName, variableName)
        {
            Owner = this
        };

        if (prompt.ShowDialog() != true)
        {
            FooterStatus = $"{server.DisplayName} 보안 값 입력이 취소되었습니다.";
            return false;
        }

        server.SetEnvironmentVariable(variableName, prompt.SecretValue);
        if (prompt.Remember)
        {
            this.credentialStore.Write(variableName, prompt.SecretValue);
        }
        else
        {
            this.credentialStore.Delete(variableName);
        }

        return true;
    }

    private async void SendCommand_Click(object sender, RoutedEventArgs e) => await SendCommandFromInputAsync();
    private async void CommandBox_KeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.Enter)
        {
            e.Handled = true;
            await SendCommandFromInputAsync();
            return;
        }

        if (e.Key == Key.Up && this.commandHistory.Count > 0)
        {
            this.commandHistoryIndex = Math.Max(0, this.commandHistoryIndex - 1);
            CommandBox.Text = this.commandHistory[this.commandHistoryIndex];
            CommandBox.CaretIndex = CommandBox.Text.Length;
            e.Handled = true;
        }
        else if (e.Key == Key.Down && this.commandHistory.Count > 0)
        {
            this.commandHistoryIndex = Math.Min(this.commandHistory.Count, this.commandHistoryIndex + 1);
            CommandBox.Text = this.commandHistoryIndex == this.commandHistory.Count ? string.Empty : this.commandHistory[this.commandHistoryIndex];
            CommandBox.CaretIndex = CommandBox.Text.Length;
            e.Handled = true;
        }

    }

    private async Task SendCommandFromInputAsync()
    {
        if (SelectedServer is null)
        {
            return;
        }

        var command = CommandBox.Text.Trim();
        if (command.Length == 0)
        {
            return;
        }

        try
        {
            await SelectedServer.SendCommandAsync(command);
            if (this.commandHistory.Count == 0 || !string.Equals(this.commandHistory[^1], command, StringComparison.Ordinal))
            {
                this.commandHistory.Add(command);
            }

            this.commandHistoryIndex = this.commandHistory.Count;
            CommandBox.Clear();
            FooterStatus = $"{SelectedServer.DisplayName}에 명령을 전송했습니다.";
        }

        catch (Exception exception)
        {
            FooterStatus = $"명령 전송 실패: {exception.Message}";
            MessageBox.Show(this, exception.Message, "서버 명령 전송", MessageBoxButton.OK, MessageBoxImage.Information);
        }

    }

    private async void StopAll_Click(object sender, RoutedEventArgs e)
    {
        FooterStatus = "전체 서버를 역순으로 종료하는 중입니다.";
        foreach (var server in Servers.OrderByDescending(server => server.StartOrder))
        {
            await server.StopAsync();
        }

        FooterStatus = "전체 종료 요청이 완료되었습니다.";
    }

    private async void Reload_Click(object sender, RoutedEventArgs e)
    {
        if (Servers.Any(server => server.IsRunning))
        {
            MessageBox.Show(this, "실행 중인 서버를 모두 중지한 뒤 설정을 다시 읽어주세요.", "설정 다시 읽기", MessageBoxButton.OK, MessageBoxImage.Information);
            return;
        }

        await LoadConfigurationAsync();
    }

    private void OpenConfig_Click(object sender, RoutedEventArgs e)
    {
        Process.Start(new ProcessStartInfo(this.configurationService.ConfigurationPath) { UseShellExecute = true });
    }

    private void ClearLogs_Click(object sender, RoutedEventArgs e)
    {
        SelectedServer?.ClearLogs();
        Categories.Clear();
        Categories.Add("전체 카테고리");
        CategoryBox.SelectedIndex = 0;
        RefreshLogView();
    }

    private async void ExportLogs_Click(object sender, RoutedEventArgs e)
    {
        if (SelectedServer is null)
        {
            return;
        }

        var dialog = new SaveFileDialog
        {
            FileName = $"{SelectedServer.Id}-{DateTime.Now:yyyyMMdd-HHmmss}.jsonl",
            Filter = "JSON Lines (*.jsonl)|*.jsonl|Text (*.txt)|*.txt"
        };

        if (dialog.ShowDialog(this) != true)
        {
            return;
        }

        await using var writer = new StreamWriter(dialog.FileName, false, new System.Text.UTF8Encoding(false));
        foreach (var log in SelectedServer.Logs)
        {
            await writer.WriteLineAsync(JsonSerializer.Serialize(log));
        }

        FooterStatus = $"로그 내보내기 완료 · {dialog.FileName}";
    }

    private void CopyLogs_Click(object sender, RoutedEventArgs e)
    {
        var selected = LogList.SelectedItems.Cast<LogEntry>().ToArray();
        CopyLogs(selected.Length > 0 ? selected : GetVisibleLogs());
    }

    private void CopySelectedLogs_Click(object sender, RoutedEventArgs e) => CopyLogs(LogList.SelectedItems.Cast<LogEntry>());
    private void CopyVisibleLogs_Click(object sender, RoutedEventArgs e) => CopyLogs(GetVisibleLogs());
    private void LogList_PreviewKeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key != Key.C || Keyboard.Modifiers != ModifierKeys.Control)
        {
            return;
        }

        e.Handled = true;
        var selected = LogList.SelectedItems.Cast<LogEntry>().ToArray();
        CopyLogs(selected.Length > 0 ? selected : GetVisibleLogs());
    }

    private LogEntry[] GetVisibleLogs() => FilteredLogs?.Cast<LogEntry>().ToArray() ?? [];
    private void CopyLogs(IEnumerable<LogEntry> logs)
    {
        var entries = logs.ToArray();
        if (entries.Length == 0)
        {
            FooterStatus = "복사할 로그가 없습니다.";
            return;
        }

        var text = string.Join(Environment.NewLine, entries.Select(log => $"{log.TimestampText}\t{log.Level}\t{log.Category}\t{log.Message}"));
        try
        {
            Clipboard.SetDataObject(text, true);
            FooterStatus = $"로그 {entries.Length:N0}개를 클립보드에 복사했습니다.";
        }

        catch (Exception exception)
        {
            FooterStatus = $"로그 복사 실패: {exception.Message}";
        }

    }

    private void LogFilter_Changed(object sender, RoutedEventArgs e)
    {
        if (sender is ToggleButton toggle && toggle.Tag is string level)
        {
            if (toggle.IsChecked == true)
            {
                this.enabledLevels.Add(level);
            }
            else
            {
                this.enabledLevels.Remove(level);
            }

        }

        RefreshLogView();
    }

    private void SearchBox_TextChanged(object sender, TextChangedEventArgs e) => RefreshLogView();
    private void CategoryBox_SelectionChanged(object sender, SelectionChangedEventArgs e) => RefreshLogView();
    private void ServerList_SelectionChanged(object sender, SelectionChangedEventArgs e) => BindSelectedLogs();
    private void RefreshLogView()
    {
        FilteredLogs?.Refresh();
        VisibleLogCount = FilteredLogs?.Cast<object>().Count() ?? 0;
    }

    private async void Window_Closing(object? sender, CancelEventArgs e)
    {
        if (this.closeAfterShutdown)
        {
            return;
        }

        var running = Servers.Where(server => server.IsRunning).ToArray();
        if (running.Length == 0)
        {
            return;
        }

        e.Cancel = true;
        var result = MessageBox.Show(this, $"관리 중인 서버 {running.Length}개를 종료하고 도구를 닫을까요?", "RPG Server Control 종료", MessageBoxButton.YesNo, MessageBoxImage.Question);
        if (result != MessageBoxResult.Yes)
        {
            return;
        }

        IsEnabled = false;
        foreach (var server in running.OrderByDescending(server => server.StartOrder))
        {
            await server.StopAsync();
        }

        this.closeAfterShutdown = true;
        Close();
    }

    private void RaiseSummary()
    {
        OnPropertyChanged(nameof(ServerCount));
        OnPropertyChanged(nameof(OnlineCount));
    }

    private void OnPropertyChanged([CallerMemberName] string? propertyName = null) => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
}

