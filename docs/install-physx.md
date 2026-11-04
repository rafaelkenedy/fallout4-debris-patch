# Install Weapon Debris with PhysX

Setup targets **Fallout 4 Steam 1.11.240.0 on Windows x64** and requires an
NVIDIA GPU with a compatible CUDA driver. The RTX 3050 was tested. The
PhysX backend is experimental and explosion force fields remain pending.

1. Sign in to GitHub and open a successful
   [Windows patch workflow run](https://github.com/rafaelkenedy/fallout4-debris-patch/actions/workflows/windows.yml)
   for the `experimental/physx-exclusive` branch. Download the artifact
   named `fallout4-debris-physx-setup-windows-x64` and extract it.
2. Close Fallout 4 and its launcher. Run `Fallout4DebrisSetup.exe`. It
   searches the Steam registry, library folders and the game's Steam
   manifest. If necessary, use **Browse** to pick the directory containing
   `Fallout4.exe`. Steam's **Manage → Browse local files** opens it directly.
3. Choose **Install PhysX patch**. Setup verifies the supported game version
   and DLL hashes, backs up the originals and extracts the embedded patch.
   The same button updates a previous installation managed by Setup. For
   a manual installation with a verified backup, choose **Restore originals**
   first and then install.
4. Launch through Steam with **Weapon Debris** enabled in the game launcher.
   `flex-compat.log` in the game folder should report
   `PhysX exclusive initialized` and an active simulation step after firing.

The executable includes the adapter, PhysX runtime, app-local Visual C++
runtime and its own .NET runtime. It works offline after download and
uses the existing NVIDIA driver. You can verify the executable with
`Get-FileHash .\Fallout4DebrisSetup.exe -Algorithm SHA256` and compare it to
the accompanying `Fallout4DebrisSetup.sha256`.

To uninstall, close the game and choose **Restore originals**. Setup verifies
the backup before restoring the original DLLs and removes its unchanged
runtime files. The backup remains available.

## Build Setup from source

Build the adapter and the pinned PhysX SDK and bridge as described in the
[developer instructions](../experiments/README.md). With the .NET 8 SDK
and Visual Studio 2022 x64 C++ redistributables installed, run from the
patch repository:

```powershell
.\build-physx-installer.ps1 -PhysXSdk 'C:\src\PhysX' -BridgeBin '..\physx-debris-build\Release'
```

The command writes `Fallout4DebrisSetup.exe` and its checksum to `dist`.
