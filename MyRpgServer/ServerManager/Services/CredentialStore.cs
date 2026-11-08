using System.ComponentModel;
using System.Runtime.InteropServices;

namespace RpgServerManager.Services;
public sealed class CredentialStore
{
    private const int GENERIC = 1;
    private const int MACHINE = 2;
    private const string PREFIX = "RpgServerManager/";
    public string? Read(string key)
    {
        var value = ReadExact(key);
        if (value is not null)
        {
            return value;
        }

        if (key == "RPG_CHARACTER_DB_PASSWORD" || key == "RPG_WORLD_DB_PASSWORD")
        {
            value = ReadExact("RPG_ACCOUNT_DB_PASSWORD") ?? ReadExact("RPG_DB_PASSWORD");
            if (value is not null)
            {
                Write(key, value);
            }

            return value;
        }

        if (key != "RPG_ACCOUNT_DB_PASSWORD")
        {
            return null;
        }

        // Preserve an existing local installation while converging the public
        // configuration name from RPG_DB_PASSWORD to RPG_ACCOUNT_DB_PASSWORD.
        value = ReadExact("RPG_DB_PASSWORD");
        if (value is null)
        {
            return null;
        }

        Write(key, value);
        Delete("RPG_DB_PASSWORD");
        return value;
    }

    private static string? ReadExact(string key)
    {
        if (!CredRead(PREFIX + key, GENERIC, 0, out var pointer))
        {
            return null;
        }

        try
        {
            var credential = Marshal.PtrToStructure<NativeCredential>(pointer);
            return credential.credentialBlob == IntPtr.Zero || credential.credentialBlobSize == 0 ? string.Empty : Marshal.PtrToStringUni(credential.credentialBlob, (int)credential.credentialBlobSize / 2);
        }

        finally
        {
            CredFree(pointer);
        }

    }

    public void Write(string key, string value)
    {
        var blob = Marshal.StringToCoTaskMemUni(value);
        try
        {
            var credential = new NativeCredential
            {
                type = GENERIC,
                targetName = PREFIX + key,
                credentialBlobSize = (uint)(value.Length * sizeof(char)),
                credentialBlob = blob,
                persist = MACHINE,
                userName = Environment.UserName,
                comment = "RPG Server Manager local secret"
            };

            if (!CredWrite(ref credential, 0))
            {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }

        }

        finally
        {
            Marshal.FreeCoTaskMem(blob);
        }

    }

    public void Delete(string key)
    {
        if (!CredDelete(PREFIX + key, GENERIC, 0))
        {
            const int FOUND = 1168;
            var error = Marshal.GetLastWin32Error();
            if (error != FOUND)
            {
                throw new Win32Exception(error);
            }

        }

    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct NativeCredential
    {
        public uint flags;
        public uint type;
        public string targetName;
        public string comment;
        public System.Runtime.InteropServices.ComTypes.FILETIME lastWritten;
        public uint credentialBlobSize;
        public IntPtr credentialBlob;
        public uint persist;
        public uint attributeCount;
        public IntPtr attributes;
        public string targetAlias;
        public string userName;
    }

    [DllImport("advapi32.dll", EntryPoint = "CredReadW", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool CredRead(string target, int type, int flags, out IntPtr credential);
    [DllImport("advapi32.dll", EntryPoint = "CredWriteW", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool CredWrite(ref NativeCredential credential, uint flags);
    [DllImport("advapi32.dll", EntryPoint = "CredDeleteW", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool CredDelete(string target, int type, int flags);
    [DllImport("advapi32.dll")]
    private static extern void CredFree(IntPtr credential);
}

