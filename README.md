# Fallout 4 Weapon Debris on Direct3D 11

An experimental Windows x64 compatibility layer for the Steam build of Fallout 4
1.11.240.0. It keeps Weapon Debris enabled and routes the game's legacy FleX calls
through NVIDIA FleX 1.2's Direct3D 11 backend.

The prototype ran inside Fallout 4 on a GeForce RTX 3050. The runtime log recorded
active simulation and particle readback, and a recorded gameplay test showed
visible fragments after firing at rocks. Compatibility across locations, collision
geometry and long sessions remains under test. Read the
[investigation and validation report](docs/investigation.md) for the evidence and
known limitations.

## Install the patch

Follow the [installation guide](docs/install.md) to download the Windows x64
package, find your Steam game folder, enable Weapon Debris and verify the test.
The guide also covers upgrading and restoring the original DLLs.

For the experimental PhysX-only backend, see the
[PhysX build and test instructions](experiments/README.md).

## Build and verify

Use a Windows x64 MinGW-w64 compiler and the NVIDIA FleX SDK at the commit pinned
in the [Windows workflow](.github/workflows/windows.yml).

```powershell
./build-flex-compat.ps1 -SdkPath 'C:\path\to\FleX'
./verify-flex-compat.ps1
```

GitHub Actions compiles both DLLs and the hardware test programs, checks the 34
game imports and six forwarders, and uploads a ZIP plus its SHA-256 checksum.
The GPU and in-game tests require a local Direct3D 11 system and remain separate
from the hosted build.

The repository contains adapter source, build scripts and documentation.
Packaged NVIDIA runtime DLLs are accompanied by the NVIDIA SDK license.
