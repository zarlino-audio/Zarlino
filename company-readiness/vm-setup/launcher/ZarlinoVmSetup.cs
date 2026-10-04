// ============================================================================
//  Zarlino VM Setup - launcher
// ----------------------------------------------------------------------------
//  A thin, auditable launcher. It does exactly three things:
//    1. extracts the embedded bootstrap (bootstrap.ps1) next to its README,
//    2. asks for elevation if it is not running elevated,
//    3. runs the bootstrap with a visible console.
//
//  Why C# instead of an NSIS installer: Defender flags unsigned NSIS stubs as
//  Trojan:Win32/Bearfoos.A!ml (a known machine-learning false positive). A
//  managed PE built with the in-box compiler does not trip that heuristic. When
//  a code-signing certificate exists, a signed NSIS build can return.
//
//  Built with:  csc.exe /target:exe /platform:anycpu /win32manifest:vm-setup.manifest
//                    /resource:bootstrap.ps1,bootstrap.ps1 /resource:README.md,README.md
// ============================================================================

using System;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Security.Principal;

internal static class BuildInfo
{
    public const string Version = "0.0.0-dev";
}

internal static class Program
{
    private const string ScriptName = "bootstrap.ps1";
    private const string ReadmeName = "README.md";
    private const string BuildHostName = "provision-buildhost.ps1";

    private static string SetupRoot()
    {
        // Per-user by default (no admin needed to unpack); the bootstrap itself
        // decides where the stack lives (C:\Zarlino by default).
        string local = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        return Path.Combine(local, "Zarlino", "setup");
    }

    private static bool IsElevated()
    {
        try
        {
            using (WindowsIdentity id = WindowsIdentity.GetCurrent())
            {
                return new WindowsPrincipal(id).IsInRole(WindowsBuiltInRole.Administrator);
            }
        }
        catch { return false; }
    }

    private static bool HasResource(string resourceName)
    {
        Assembly asm = Assembly.GetExecutingAssembly();
        foreach (string n in asm.GetManifestResourceNames())
        {
            if (n == resourceName) return true;
        }
        return false;
    }

    private static void Extract(string resourceName, string targetPath)
    {
        Assembly asm = Assembly.GetExecutingAssembly();
        using (Stream s = asm.GetManifestResourceStream(resourceName))
        {
            if (s == null) throw new InvalidOperationException("embedded resource missing: " + resourceName);
            string dir = Path.GetDirectoryName(targetPath);
            if (!string.IsNullOrEmpty(dir)) Directory.CreateDirectory(dir);
            using (FileStream fs = new FileStream(targetPath, FileMode.Create, FileAccess.Write, FileShare.None))
            {
                s.CopyTo(fs);
            }
        }
    }

    private static int Main(string[] args)
    {
        bool extractOnly = false;
        foreach (string a in args)
        {
            if (a.Equals("--extract-only", StringComparison.OrdinalIgnoreCase)) extractOnly = true;
            if (a.Equals("--version", StringComparison.OrdinalIgnoreCase) || a.Equals("-v", StringComparison.OrdinalIgnoreCase))
            {
                Console.WriteLine("Zarlino VM Setup " + BuildInfo.Version);
                return 0;
            }
        }

        Console.Title = "Zarlino VM Setup " + BuildInfo.Version;
        Console.WriteLine("Zarlino VM Setup " + BuildInfo.Version);
        Console.WriteLine("=================================");

        string root = SetupRoot();
        string script = Path.Combine(root, ScriptName);
        string readme = Path.Combine(root, ReadmeName);
        string buildHost = Path.Combine(root, BuildHostName);

        try
        {
            Extract(ScriptName, script);
            Extract(ReadmeName, readme);
            if (HasResource(BuildHostName)) Extract(BuildHostName, buildHost);
        }
        catch (Exception ex)
        {
            Console.WriteLine("FAIL could not unpack the setup script: " + ex.Message);
            return 3;
        }

        Console.WriteLine("    OK   setup files unpacked to " + root);

        if (extractOnly)
        {
            Console.WriteLine("    ..   extract-only mode: nothing else was done");
            return 0;
        }

        // Forward any extra arguments to the bootstrap (e.g. -BuildHost, -DryRun).
        string extraArgs = "";
        foreach (string a in args)
        {
            if (a.Equals("--extract-only", StringComparison.OrdinalIgnoreCase)) continue;
            if (a.Equals("--version", StringComparison.OrdinalIgnoreCase)) continue;
            if (a.Equals("-v", StringComparison.OrdinalIgnoreCase)) continue;
            extraArgs += " " + a;
        }

        if (!IsElevated())
        {
            Console.WriteLine("    ..   not elevated - asking Windows for permission (UAC prompt)");
            try
            {
                ProcessStartInfo relaunch = new ProcessStartInfo(Assembly.GetExecutingAssembly().Location)
                {
                    UseShellExecute = true,
                    Verb = "runas"
                };
                Process.Start(relaunch);
                return 0;
            }
            catch (Exception ex)
            {
                Console.WriteLine("FAIL elevation was refused or failed: " + ex.Message);
                Console.WriteLine("     Right-click this file and choose 'Run as administrator'.");
                Console.WriteLine("     You can also run the script directly: powershell -ExecutionPolicy Bypass -File \"" + script + "\"");
                return 4;
            }
        }

        // The bootstrap is intentionally a script: readable, re-runnable, and it
        // supports -DryRun so the operator can preview every action.
        ProcessStartInfo psi = new ProcessStartInfo
        {
            FileName = "powershell.exe",
            Arguments = "-NoProfile -ExecutionPolicy Bypass -NoExit -File \"" + script + "\"" + extraArgs,
            UseShellExecute = false
        };
        Process p = Process.Start(psi);
        p.WaitForExit();
        return p.ExitCode;
    }
}
