# Install the Fallout 4 Weapon Debris patch

The packaged patch targets **Windows x64 with Steam Fallout 4 1.11.240.0**
and requires a Direct3D 11-capable GPU and driver. Gameplay has been tested
on a GeForce RTX 3050. Other GPUs, game versions, storefronts, and Linux
through Proton are unverified. The Steam library can be on any drive.

## 1. Download the package

Sign in with a GitHub account that has access to the repository. Open the
[Windows patch workflow](https://github.com/rafaelkenedy/fallout4-debris-patch/actions/workflows/windows.yml),
select the latest successful run on `main`, and download
`fallout4-debris-patch-windows-x64` from its **Artifacts** section. Extract
the download. It contains `fallout4-debris-patch.zip` and `SHA256SUMS.txt`.

To verify the package, open PowerShell in that folder and run these commands.
The last command should print `True`.

```powershell
$expected = ((Get-Content -LiteralPath .\SHA256SUMS.txt -TotalCount 1) -split '\s+')[0]
(Get-FileHash -LiteralPath .\fallout4-debris-patch.zip -Algorithm SHA256).Hash -eq $expected
```

Extract `fallout4-debris-patch.zip` into a folder you can write to. Keep
`flex-compat-install.ps1` beside the `flex-compat-stage` folder. The ZIP
includes the runtime required by the patch.

## 2. Find the game and install

In Steam, right-click **Fallout 4** in your Library and choose **Manage →
Browse local files**. Copy the folder path from File Explorer. The folder
should contain `Fallout4.exe`. Right-click the executable and choose
**Properties → Details** to check the file version.

Close the game and its launcher. Open PowerShell in the extracted patch folder
and replace the example path with the Steam folder you copied.

```powershell
$game = 'D:\SteamLibrary\steamapps\common\Fallout 4'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\flex-compat-install.ps1 -Mode Install -GameDirectory $game
```

The execution-policy option applies to that PowerShell process. The installer
checks the two original FleX DLLs and saves verified copies in
`flex-compat-original` inside the game folder. If it reports a DLL hash
mismatch, check the game version and whether another Weapon Debris fix has
replaced the originals. For an earlier build of this patch, use the restore
command below before installing the new package.

## 3. Enable Weapon Debris and check the log

In the Fallout 4 launcher, enable **Weapon Debris** in **Options → Advanced**.
If you configure the game through its INI, check that the `[NVFlex]` section of
`Documents\My Games\Fallout4\Fallout4Prefs.ini` contains `bNVFlexEnable=1`.

Launch the game through Steam, load a save, and fire at terrain or a solid
surface that produces debris. Open `flex-compat.log` in the game folder. Look
for these lines near the end after the test.

```text
FleX D3D11 initialized.
FleX D3D11 submitted its first active simulation step.
FleX D3D11 particle readback succeeded.
```

If the scene submits a convex with more than 63 distinct planes, the log can
also show `submitted reduced convex` with the input, distinct, and retained
plane counts. The log reports the first successful reduction per solver.

## Restore or upgrade

Close the game and its launcher. From the extracted package folder, set
`$game` to your Steam game path as above and run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\flex-compat-install.ps1 -Mode Restore -GameDirectory $game
```

The installer restores and verifies both original DLLs from
`flex-compat-original`. To upgrade, then run the install command from the
new package folder.
