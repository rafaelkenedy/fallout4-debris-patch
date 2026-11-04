using System.Diagnostics;
using System.IO.Compression;
using System.Security.Cryptography;
using System.Text.Json;

internal static class Patch
{
    private const string Backup = "flex-compat-original";
    private const string Marker = "physx-exclusive";
    private const string State = "physx-debris-install.json";
    private const string Runtime = "physx-debris-runtime/";
    private static readonly Dictionary<string, string> Originals = new()
    {
        ["flexRelease_x64.dll"] = "F1C8B20A685BC868EC936F7CAF3EF62805A25B7D6A26E0A5AADD28B323141B9F",
        ["flexExtRelease_x64.dll"] = "C9E8D458F6E0181387ACB899EE8CD68A00695731AD262100040096FE6C48A1B6",
    };
    private static readonly string[] Payload = Originals.Keys.Concat(new[]
    {
        "physx-debris-bridge.dll", "PhysX_64.dll", "PhysXCommon_64.dll",
        "PhysXCooking_64.dll", "PhysXFoundation_64.dll", "PhysXGpu_64.dll",
        "msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll", "NVIDIA-LICENSE.md",
    }.Select(name => Runtime + name)).ToArray();

    public static void Install(string game)
    {
        game = ValidateGame(game);
        using var installLock = AcquireLock(game);
        EnsureClosed();
        string backup = Path.Combine(game, Backup);
        var installed = ReadState(game);
        foreach (var (name, original) in Originals)
        {
            string? current = HashIfPresent(Path.Combine(game, name));
            if (current != original && (installed?.GetValueOrDefault(name) is not string known || current != known))
                throw new InvalidOperationException($"Unexpected {name}. Restore the original DLLs before installing.");
        }
        using var transaction = new Transaction(game);
        using var archive = new ZipArchive(typeof(Patch).Assembly.GetManifestResourceStream("patch.zip")
            ?? throw new InvalidDataException("The installer has no embedded patch."), ZipArchiveMode.Read);
        var hashes = new Dictionary<string, string>(StringComparer.Ordinal);
        foreach (string name in Payload)
        {
            var entry = archive.GetEntry(name) ?? throw new InvalidDataException($"Missing embedded file {name}.");
            string staged = transaction.NewFile();
            using (var input = entry.Open())
            using (var output = File.Create(staged))
                input.CopyTo(output);
            hashes[name] = Hash(staged);
            transaction.Add(Path.Combine(game, name), staged);
        }
        foreach (var (name, original) in Originals)
        {
            string target = Path.Combine(backup, name);
            if (File.Exists(target))
            {
                if (Hash(target) != original) throw new InvalidDataException($"The backup of {name} failed verification.");
            }
            else
            {
                string source = Path.Combine(game, name);
                if (HashIfPresent(source) != original) throw new InvalidDataException($"Missing original backup for {name}.");
                transaction.Add(target, source, first: true);
            }
        }
        string marker = transaction.NewFile();
        File.WriteAllBytes(marker, []);
        string state = transaction.NewFile();
        File.WriteAllText(state, JsonSerializer.Serialize(hashes));
        transaction.Add(Path.Combine(game, State), state);
        transaction.Add(Path.Combine(game, Marker), marker);
        transaction.Apply();
    }

    public static void Restore(string game)
    {
        game = ValidateGame(game);
        using var installLock = AcquireLock(game);
        EnsureClosed();
        string backup = Path.Combine(game, Backup);
        var installed = ReadState(game);
        using var transaction = new Transaction(game);
        foreach (var (name, original) in Originals)
        {
            string target = Path.Combine(game, name);
            if (HashIfPresent(target) == original) continue;
            string source = Path.Combine(backup, name);
            if (HashIfPresent(source) != original) throw new InvalidDataException($"No verified original backup for {name}.");
            transaction.Add(target, source);
        }
        foreach (string name in Payload.Where(p => p.StartsWith(Runtime, StringComparison.Ordinal)))
        {
            string target = Path.Combine(game, name);
            if (installed?.GetValueOrDefault(name) is string known && HashIfPresent(target) == known)
                transaction.Add(target, null);
        }
        transaction.Add(Path.Combine(game, Marker), null);
        transaction.Add(Path.Combine(game, State), null);
        transaction.Apply();
    }

    private static Dictionary<string, string>? ReadState(string game)
    {
        string path = Path.Combine(game, State);
        return File.Exists(path) ? JsonSerializer.Deserialize<Dictionary<string, string>>(File.ReadAllText(path)) : null;
    }

    private static string ValidateGame(string path)
    {
        string game = Path.GetFullPath(path.Trim());
        string exe = Path.Combine(game, "Fallout4.exe");
        if (!File.Exists(exe)) throw new DirectoryNotFoundException("Select the folder containing Fallout4.exe.");
        var v = FileVersionInfo.GetVersionInfo(exe);
        if (v.FileMajorPart != 1 || v.FileMinorPart != 11 || v.FileBuildPart != 240 || v.FilePrivatePart != 0)
            throw new InvalidOperationException($"This installer supports Fallout 4 1.11.240.0. Detected {v.FileVersion}.");
        return game;
    }

    private static void EnsureClosed()
    {
        foreach (string name in new[] { "Fallout4", "Fallout4Launcher" })
        {
            var processes = Process.GetProcessesByName(name);
            foreach (var process in processes) process.Dispose();
            if (processes.Length > 0) throw new InvalidOperationException("Close Fallout 4 and its launcher first.");
        }
    }

    private static string? HashIfPresent(string path) => File.Exists(path) ? Hash(path) : null;
    private static FileStream AcquireLock(string game) =>
        new(Path.Combine(game, ".physx-debris-setup.lock"), FileMode.OpenOrCreate,
            FileAccess.ReadWrite, FileShare.None, 1, FileOptions.DeleteOnClose);

    private static string Hash(string path)
    {
        using var stream = File.OpenRead(path);
        return Convert.ToHexString(SHA256.HashData(stream));
    }

    private sealed class Transaction : IDisposable
    {
        private readonly string temp;
        private readonly List<(string Target, string? Source)> changes = [];
        private bool keepBackup;

        public Transaction(string game) =>
            temp = Directory.CreateDirectory(Path.Combine(game, ".debris-setup-" + Guid.NewGuid().ToString("N"))).FullName;

        public string NewFile() => Path.Combine(temp, Guid.NewGuid().ToString("N"));
        public void Add(string target, string? source, bool first = false)
        {
            if (first) changes.Insert(0, (target, source));
            else changes.Add((target, source));
        }

        public void Apply()
        {
            EnsureClosed();
            var saved = new Stack<(string Target, string? Backup)>();
            try
            {
                foreach (var (target, source) in changes)
                {
                    if (source is null && !File.Exists(target)) continue;
                    string? previous = null;
                    if (File.Exists(target))
                    {
                        previous = NewFile();
                        File.Copy(target, previous);
                    }
                    Replace(target, source);
                    saved.Push((target, previous));
                }
            }
            catch (Exception error)
            {
                try
                {
                    while (saved.TryPop(out var entry)) Replace(entry.Target, entry.Backup);
                }
                catch (Exception rollback)
                {
                    keepBackup = true;
                    throw new IOException($"Rollback failed. Recovery files are in {temp}.", new AggregateException(error, rollback));
                }
                throw;
            }
        }

        private void Replace(string target, string? source)
        {
            if (source is null) { File.Delete(target); return; }
            Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            string staged = NewFile();
            File.Copy(source, staged);
            if (Hash(staged) != Hash(source)) throw new IOException($"Verification failed for {target}.");
            File.Move(staged, target, overwrite: true);
        }

        public void Dispose()
        {
            if (keepBackup) return;
            try { Directory.Delete(temp, recursive: true); }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
        }
    }
}
