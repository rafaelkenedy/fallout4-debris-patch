# PhysX 5 debris feasibility test

## PhysX-only gameplay build

The opt-in `physx-exclusive` mode provides the exports expected by the
Fallout 4 Steam x64 build 1.11.240.0. It initializes PhysX 5.9 with CUDA
without loading the FleX runtime and runs without the 60-frame limit used
by the earlier diagnostic probe. The replacement DLLs keep the names
expected by the game.

On the RTX 3050, an isolated loader checked that `PhysXGpu_64.dll` was loaded
and that the FleX D3D11 and legacy CUDA runtime modules were absent. It
verified host rigid-asset creation and simulated and read back particles
after 60 steps. A Fallout 4 run in exclusive mode accepted 85 of 85
colliders in the initial logged submission, completed active simulation
and returned particle positions. The captured window recorded 40 active
updates and reached 101 particles in 16 rigid groups. These counts describe
the trace window rather than a limit in the solver. No Fallout 4 application
failure or NVIDIA driver event was found during that run.

The backend approximates mesh assets by sampling their vertices and uses
sphere collision shapes for particles and rigid-group members. FleX phase
filters, solver planes and explosion force fields remain unimplemented.
Visual parity, frame time, long-session behavior and the RTX 5080 still
need gameplay validation.

### Build and install locally

For a single-file installer, use the
[PhysX setup guide](../docs/install-physx.md). The manual procedure below
is useful when iterating on a development build.

1. Build the PhysX SDK and the bridge using the [Windows x64 commands below](#build-on-windows-x64).
   Use CUDA Toolkit 12.8 for the pinned SDK. The local test used CUDA 12.8
   with NVIDIA display driver 621.01.
2. Build the game-facing DLLs using the
   [repository build instructions](../README.md#build-and-verify). The FleX
   SDK headers are used to build the compatibility layer. The exclusive
   mode only loads the PhysX runtime.
3. Close Fallout 4 and its launcher. From the repository root, run the
   [standard installer](../docs/install.md#2-find-the-game-and-install)
   against a clean copy of the supported Steam build. If an earlier build
   is installed, restore the originals first. The installer creates a
   verified backup.
4. Use the same `$game` path as in the standard guide and run the following
   from the patch repository root:

```powershell
$runtime = Join-Path $game 'physx-debris-runtime'
New-Item -ItemType Directory -Path $runtime -Force | Out-Null
Copy-Item -Path ../physx-debris-build/Release/PhysX*.dll -Destination $runtime
Rename-Item -LiteralPath (Join-Path $game 'flex-compat-runtime') -NewName 'flex-compat-runtime.disabled'
New-Item -ItemType File -Path (Join-Path $game 'physx-exclusive') | Out-Null
```

The runtime directory must contain the bridge and the five PhysX SDK DLLs,
including `PhysXGpu_64.dll`. Launch through Steam with Weapon Debris enabled.
Check `flex-compat.log` for `PhysX exclusive initialized`, a completed active
step and particle position readback. If initialization fails, check that the
bridge and SDK binaries were built from the same SDK configuration.

To return to the D3D11 backend, close the game, rename the `physx-exclusive`
marker, and rename `flex-compat-runtime.disabled` back to
`flex-compat-runtime`. To restore the original game DLLs, use the standard
installer's [restore command](../docs/install.md#restore-or-upgrade).

The Windows workflow builds the compatibility layer, then compiles and
packages this PhysX configuration into Setup. GPU tests run on a local
CUDA-capable machine.

## Collision and solver tests

This standalone test uses PhysX 5.9.0 GPU rigid bodies and GPU broad phase.
It cooks a static triangle mesh with GPU data. The mesh represents a
synthetic 125-face prism with 123 sides and two caps. A box should settle
on the top at approximately y=0.575. A second box outside the prism
should fall freely. The executable fails if CUDA initialization, GPU scene
creation, or the position checks fail.

The test supplies legacy-format plane equations to `PlaneMesh`. The converter
rejects the open prism before the last cap is added. It reconstructs the closed
prism as 246 vertices and 488 triangles using all 125 planes. The result is
cooked as a static triangle mesh with GPU collision data.

The test also accepts one raw capture file. Create the empty
`flex-capture-convex` marker next to the instrumented game DLL before launch.
It saves the first convex with 125–256 planes to `flex-convex-planes.bin`
without overwriting an existing capture. Each plane is four little-endian
32-bit floats. Pass that file as the executable's sole argument. In capture
mode, a sphere falls toward the largest triangle and the test requires a
contact notification.

A 125-plane capture from Fallout 4 reconstructed to 245 vertices and 486
triangles. PhysX reported contact in the GPU scene on the RTX 3050. This
checks a real collider offline. The adapter still needs to translate the
FleX ABI in both directions and cache cooked geometry before gameplay testing.

## Standalone particle backend

Run `physx-debris-smoke.exe --particles` to exercise the experimental particle
channels directly through PhysX. The test uploads game-style positions, inverse
masses, velocities and a sparse active list. It checks gravity, contact with a
plane, position and velocity readback, deactivation and slot reuse. This mode
passed on the RTX 3050. The executable imports PhysX libraries and does not
require a FleX runtime.

Run `physx-debris-smoke.exe --rigids` to test a group of particles as a
single compound rigid body. It uses the supplied local offsets for its
collision shapes and reconstructs the particle positions and velocities from
the body pose and motion. On the RTX 3050, the group settled at y=0.075000,
rotated to quaternion z=-0.382684 and retained the original 0.424264
distance between its members. The test repeats the game's position, velocity,
active-list and rigid-group uploads for 120 frames. Deactivating one member
invalidates the old group before the next upload.

This backend models individual and grouped particles with sphere collision
shapes. The adapter still needs to route the game-facing calls to it and
implement asset generation, the full shape list and force fields.

## Gameplay call order

A D3D11 trace on the RTX 3050 captured the first active frame after firing.
Fallout 4 submitted 11 active slots and eight rigid groups with 11 member
indices, then requested two simulation substeps. The following frame read
particle positions, velocities and rigid transforms, then uploaded shapes,
particles, velocities, phases, the active list and the rigid groups again.
The eight groups were resubmitted on successive frames. The first active
window included 67 shapes and 1,484 geometry records in one call, and no
nonempty force-field upload was observed in that window.

The standalone backend treats recurring uploads as updates. It keeps an
existing actor when the member slots, rest offsets, mass and returned pose
match. This preserves angular velocity and contact state across reuploads.
The gameplay trace establishes call order and counts. Stable game group IDs
and the behavior of the matching rule on live uploads remain unverified.

On a GeForce RTX 3050 with driver 621.01, the PhysX 5.9.0 release build
compiled with CUDA 12.8 and passed this test. After 120 steps, the fragment
landed at y=0.574999 and the control fell to y=-17.783491.

## Build on Windows x64

Use the NVIDIA PhysX SDK tag `110.1-omni-and-physx-5.9.0`, Visual Studio 2022
and the CUDA Toolkit required by that SDK. From the SDK's `physx` folder:

```powershell
.\generate_projects.bat vc17win64
cmake --build .\compiler\vc17win64 --config release --target PhysX PhysXCooking PhysXExtensions PhysXGpu
```

From the patch repository, point CMake to that SDK's `physx` folder and the
directory containing its x64 release DLLs and libraries:

```powershell
cmake -S experiments -B ../physx-debris-build -G 'Visual Studio 17 2022' -A x64 `
  -DPHYSX_SDK='C:/src/PhysX/physx' -DPHYSX_BIN='C:/path/to/physx/release'
cmake --build ../physx-debris-build --config Release
../physx-debris-build/Release/physx-debris-smoke.exe
```
