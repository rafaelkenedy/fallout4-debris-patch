# Fallout 4 Weapon Debris Patch

A Windows x64 compatibility layer for Steam Fallout 4 1.11.240.0. The
recommended PhysX backend runs Weapon Debris on NVIDIA PhysX 5.9 with CUDA.

The prototype ran inside Fallout 4 on a GeForce RTX 3050. The runtime log recorded
active simulation and particle readback, and a recorded gameplay test showed
visible fragments after firing at rocks. Compatibility across locations, collision
geometry and long sessions remains under test. Read the
[investigation and validation report](docs/investigation.md) for the evidence and
known limitations.

## Install the PhysX patch

Download `Fallout4DebrisSetup.exe` from the [latest release](https://github.com/rafaelkenedy/fallout4-debris-patch/releases/latest).
Setup detects Steam libraries, includes the runtime and can restore the
original game DLLs. See the [installation guide](docs/install-physx.md) for
supported hardware, setup and verification.

## Legacy D3D11 backend

The FleX D3D11 build is **deprecated**. Its
[installation guide](docs/install.md) remains available for existing users.

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
