using Microsoft.Win32;
using System.Text.RegularExpressions;

internal static partial class Steam
{
    public static IEnumerable<string> FindGames()
    {
        var games = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var libraries = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        AddLibrary(libraries, Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Steam"));
        foreach (var view in new[] { RegistryView.Registry32, RegistryView.Registry64 })
        {
            using var user = RegistryKey.OpenBaseKey(RegistryHive.CurrentUser, view);
            using var machine = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, view);
            using var userSteam = user.OpenSubKey(@"Software\Valve\Steam");
            using var machineSteam = machine.OpenSubKey(@"Software\Valve\Steam");
            using var app = machine.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 377160");
            AddLibrary(libraries, userSteam?.GetValue("SteamPath") as string);
            AddLibrary(libraries, machineSteam?.GetValue("InstallPath") as string);
            AddGame(games, app?.GetValue("InstallLocation") as string);
        }

        foreach (string root in libraries.ToArray())
        {
            foreach (var pair in ReadPairs(Path.Combine(root, "steamapps", "libraryfolders.vdf")))
            {
                // Modern Steam uses a path key. Older manifests use numbered string keys.
                if (pair.Key.Equals("path", StringComparison.OrdinalIgnoreCase) || int.TryParse(pair.Key, out _))
                    AddLibrary(libraries, pair.Value);
            }
        }
        foreach (string library in libraries)
        {
            string apps = Path.Combine(library, "steamapps");
            string common = Path.Combine(apps, "common");
            string? name = ReadPairs(Path.Combine(apps, "appmanifest_377160.acf"))
                .FirstOrDefault(p => p.Key.Equals("installdir", StringComparison.OrdinalIgnoreCase)).Value;
            if (!string.IsNullOrWhiteSpace(name) && name is not "." and not ".." &&
                name.IndexOfAny(Path.GetInvalidFileNameChars()) < 0)
                AddGame(games, Path.Combine(common, name));
            AddGame(games, Path.Combine(common, "Fallout 4"));
        }
        return games.OrderBy(p => p, StringComparer.OrdinalIgnoreCase);
    }

    private static void AddLibrary(HashSet<string> libraries, string? path)
    {
        if (!string.IsNullOrWhiteSpace(path) && Path.IsPathFullyQualified(path) && Directory.Exists(path))
            libraries.Add(Path.GetFullPath(path));
    }

    private static void AddGame(HashSet<string> games, string? path)
    {
        if (!string.IsNullOrWhiteSpace(path) && Path.IsPathFullyQualified(path) &&
            File.Exists(Path.Combine(path, "Fallout4.exe")))
            games.Add(Path.GetFullPath(path));
    }

    internal static IEnumerable<KeyValuePair<string, string>> ReadPairs(string path)
    {
        string data;
        try { data = File.ReadAllText(path); }
        catch (IOException) { yield break; }
        catch (UnauthorizedAccessException) { yield break; }
        foreach (Match match in Pairs().Matches(data))
            yield return new(Unescape(match.Groups[1].Value), Unescape(match.Groups[2].Value));
    }

    private static string Unescape(string value) => value.Replace(@"\\", @"\").Replace("\\\"", "\"");

    [GeneratedRegex(@"""((?:\\.|[^""\\])*)""\s+""((?:\\.|[^""\\])*)""")]
    private static partial Regex Pairs();
}
