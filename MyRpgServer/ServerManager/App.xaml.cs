using System.Windows;
using System.Windows.Threading;

namespace RpgServerManager;
public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        DispatcherUnhandledException += OnDispatcherUnhandledException;
        AppDomain.CurrentDomain.UnhandledException += OnUnhandledException;
        TaskScheduler.UnobservedTaskException += OnUnobservedTaskException;
        base.OnStartup(e);
    }

    private void OnDispatcherUnhandledException(object sender, DispatcherUnhandledExceptionEventArgs e)
    {
        var crashLogPath = WriteCrashLog("UI", e.Exception);
        MessageBox.Show($"서버 관리자 실행 중 오류가 발생했습니다.\n\n오류 기록: {crashLogPath}\n\n{e.Exception.Message}", "RPG Server Manager", MessageBoxButton.OK, MessageBoxImage.Error);
        e.Handled = true;
        Shutdown(-1);
    }

    private static void OnUnhandledException(object sender, UnhandledExceptionEventArgs e)
    {
        if (e.ExceptionObject is Exception exception)
        {
            WriteCrashLog("Runtime", exception);
        }

    }

    private static void OnUnobservedTaskException(object? sender, UnobservedTaskExceptionEventArgs e)
    {
        WriteCrashLog("Task", e.Exception);
        e.SetObserved();
    }

    private static string WriteCrashLog(string source, Exception exception)
    {
        try
        {
            var logDirectory = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "RpgServerManager");
            Directory.CreateDirectory(logDirectory);
            var logPath = Path.Combine(logDirectory, "crash.log");
            File.AppendAllText(logPath, $"[{DateTimeOffset.Now:O}] [{source}]\n{exception}\n\n");
            return logPath;
        }

        catch
        {
            return "오류 기록 파일을 만들 수 없습니다.";
        }

    }

}

