using System.Windows;

namespace RpgServerManager;
public partial class SecretPromptWindow : Window
{
    public SecretPromptWindow(string serverName, string variableName)
    {
        InitializeComponent();
        VariableName = variableName;
        DescriptionText.Text = $"{serverName} 실행에 필요한 {variableName} 값을 입력하세요.";
        Loaded += (_, _) => SecretBox.Focus();
    }

    public string VariableName { get; }
    public string SecretValue => SecretBox.Password;
    public bool Remember => RememberBox.IsChecked == true;

    private void Accept_Click(object sender, RoutedEventArgs e)
    {
        if (string.IsNullOrWhiteSpace(SecretBox.Password))
        {
            MessageBox.Show(this, "값을 입력해주세요.", "서버 보안 설정", MessageBoxButton.OK, MessageBoxImage.Information);
            return;
        }

        DialogResult = true;
    }

    private void Cancel_Click(object sender, RoutedEventArgs e) => DialogResult = false;
}

