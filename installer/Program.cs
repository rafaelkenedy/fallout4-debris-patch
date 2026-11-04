internal static class Program
{
    [STAThread]
    private static int Main(string[] args)
    {
        if (args.Length > 0)
        {
            try
            {
                if (args is ["--find"]) { foreach (string game in Steam.FindGames()) Console.WriteLine(game); }
                else if (args is ["--install", var install]) Patch.Install(install);
                else if (args is ["--restore", var restore]) Patch.Restore(restore);
                else return 2;
                return 0;
            }
            catch (Exception error) { Console.Error.WriteLine(error.Message); return 1; }
        }

        ApplicationConfiguration.Initialize();
        Application.Run(new SetupWindow());
        return 0;
    }
}

internal sealed class SetupWindow : Form
{
    private readonly ComboBox folder = new() { Dock = DockStyle.Fill };
    private readonly Button browse = new() { Text = "Browse…", AutoSize = true };
    private readonly Button install = new() { Text = "Install PhysX patch", AutoSize = true };
    private readonly Button restore = new() { Text = "Restore originals", AutoSize = true };
    private readonly Label status = new() { AutoSize = true, Dock = DockStyle.Fill };
    private bool busy;

    public SetupWindow()
    {
        Text = "Fallout 4 Weapon Debris Setup";
        ClientSize = new Size(640, 220);
        MinimumSize = new Size(525, 250);
        StartPosition = FormStartPosition.CenterScreen;

        var layout = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(16), ColumnCount = 2, RowCount = 4 };
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        for (int row = 0; row < 4; row++) layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        var header = new Label { Text = "Fallout 4 game folder", AutoSize = true, Margin = new Padding(3, 6, 3, 8) };
        layout.Controls.Add(header, 0, 0);
        layout.SetColumnSpan(header, 2);
        layout.Controls.Add(folder, 0, 1);
        layout.Controls.Add(browse, 1, 1);
        var actions = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true, Margin = new Padding(0, 12, 0, 8) };
        actions.Controls.AddRange([install, restore]);
        layout.Controls.Add(actions, 0, 2);
        layout.SetColumnSpan(actions, 2);
        layout.Controls.Add(status, 0, 3);
        layout.SetColumnSpan(status, 2);
        Controls.Add(layout);

        browse.Click += (_, _) =>
        {
            using var dialog = new FolderBrowserDialog { Description = "Select the folder containing Fallout4.exe." };
            if (Directory.Exists(folder.Text)) dialog.InitialDirectory = folder.Text;
            if (dialog.ShowDialog(this) == DialogResult.OK) folder.Text = dialog.SelectedPath;
        };
        install.Click += async (_, _) => await RunPatch(false);
        restore.Click += async (_, _) =>
        {
            if (MessageBox.Show(this, "Restore the verified original game DLLs?", Text,
                MessageBoxButtons.YesNo, MessageBoxIcon.Question) == DialogResult.Yes) await RunPatch(true);
        };
        FormClosing += (_, e) => { if (busy) e.Cancel = true; };
        Shown += (_, _) =>
        {
            try
            {
                folder.Items.AddRange(Steam.FindGames().Cast<object>().ToArray());
                if (folder.Items.Count > 0) folder.SelectedIndex = 0;
                status.Text = folder.Items.Count > 0
                    ? "Steam installation detected. Close the game and its launcher before continuing."
                    : "Choose the folder using Browse. In Steam, use Manage → Browse local files.";
            }
            catch (Exception) { status.Text = "Steam detection failed. Select the game folder manually."; }
        };
    }

    private async Task RunPatch(bool restoreOriginals)
    {
        string game = folder.Text.Trim();
        if (game.Length == 0) { status.Text = "Select the folder containing Fallout4.exe."; return; }
        busy = true;
        folder.Enabled = browse.Enabled = install.Enabled = restore.Enabled = false;
        status.Text = restoreOriginals ? "Restoring and verifying original files…" : "Extracting and verifying the embedded patch…";
        try
        {
            await Task.Run(() => { if (restoreOriginals) Patch.Restore(game); else Patch.Install(game); });
            status.Text = restoreOriginals ? "Original game DLLs restored."
                : "PhysX patch installed. Launch through Steam with Weapon Debris enabled.";
        }
        catch (UnauthorizedAccessException) { status.Text = "Access denied. Check the folder permissions or run Setup as administrator."; }
        catch (Exception error) { status.Text = error.Message; }
        finally
        {
            busy = false;
            folder.Enabled = browse.Enabled = install.Enabled = restore.Enabled = true;
        }
    }
}
