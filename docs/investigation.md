# Weapon Debris crash investigation

## Reproduction and initial evidence

The affected installation was Fallout 4 Steam x64 1.11.240.0 on Windows 10
19045, with a GeForce RTX 3050 and driver version 32.0.16.2101. Steam repeatedly
reported process exit `0xC0000005` after an NVIDIA `nvlddmkm` event 153
(`Error occurred on GPUID: 100`). The sequence occurred with Weapon Debris
enabled at different quality levels and was also observed in an earlier run
with the effect disabled. The disabled case has no conclusive dump.

A ProcDump capture from a run with the effect enabled identified
`flexRelease_x64.dll+0x27AE3` as the exception instruction. The CPU attempted a
write to address zero in a fill routine. Its caller requested 120 bytes through
`flexAlloc`, which called `cudaMallocHost` through the bundled
`cudart64_75.dll`, and passed the returned pointer to the fill routine.
Other early ProcDump captures contained the legacy thread-name exception
`0x406D1388` and did not represent this access violation.

An in-process debugger probe caught the error path at `flexRelease_x64.dll+0x7D5A`.
It recorded CUDA status **77**, corresponding to `cudaErrorIllegalAddress` in
the legacy runtime, and a null buffer. This error can surface on a later CUDA
call after an illegal GPU memory access. The probe did not identify the first
failing GPU instruction. Three earlier error-check sites did not record an
error before the allocation in a subsequent capture. A launch-blocking trial
was inconclusive because Steam relaunched the launcher and inheritance of the
temporary environment variable could not be verified.

Primary reference: [NVIDIA CUDA Runtime API](https://docs.nvidia.com/cuda/archive/10.0/pdf/CUDA_Runtime_API.pdf).

## Static analysis

REA import and export inspection established that the executable imports
28 core FleX functions and six extension functions. The original extension
also imports CUDA 7.5, so replacing only the core DLL would leave a legacy
CUDA dependency. The game initializes legacy API version 90 with device
selection `-1`. It passes the configured maximum neighbor count as a byte
to solver creation, uses memory mode 2 for the gameplay transfers examined,
and advances the solver with two substeps and a null timer pointer.

The analysis identified a 328-byte solver parameter block, 16-byte legacy
shape geometry records, 128-byte rigid asset layout, and 44-byte force-field
records. The original extension's embedded PTX exposed the force-field
radius, strength range, direction, mode and falloff offsets. Legacy shape
types include sphere, capsule, plane-defined convex and triangle mesh.

Selected REA evidence from the investigation:

| Evidence | ID |
| --- | --- |
| Fallout 4 imports | `ev_48c2b34ee04b7bf7f0b8193e9ec512e78d79d0834364189ae3260f7079ba48c7` |
| Original FleX imports | `ev_7ef3daed7daff65fb05f721e058a20b855387469288befe873ca385038dd5565` |
| Original extension imports | `ev_9d88db6c26942e877bf32411e143690e36637086b6cdcce498462a333718c399` |
| Original extension GPU objects | `ev_4b373410fc657dffb39f3bb085d4fbec50c0019951711aec4c024f1f2ad77902` |

## Compatibility design

The core shim owns the solver and staging buffers. The extension shim forwards
its six exports to the same core DLL, keeping container and solver ownership in
one module. The bootstrap loads the pinned NVIDIA Direct3D 11 runtime from a
dedicated directory. CPU pointers remain stable for the legacy particle
container. The adapter translates particle transfers, phases, bounds,
solver parameters, triangle meshes, collision shapes and rigid transforms.
The legacy force-field calculation runs on the CPU with GPU buffer readback
and upload when active. Snapshot and recording functions emit an adapter-specific
diagnostic format.

## Verification and limits

- Standalone tests ran on the RTX 3050 and verified gravity, particle
  upload/readback, collision against four representative shape types,
  rigid-body transforms, mesh-to-rigid generation, force application and
  diagnostic recording.
- A separate loader exercised the compiled pair of DLLs and extension
  forwarders with a solver capacity of 32,768 and 64 neighbors.
- Fallout 4 initialized the adapter and logged active simulation and readback
  on two solvers. A roughly 20-second recording showed fragments separating
  from the impact dust after shots against rocks, with no gameplay crash
  visible in the clip.
- The original kernel walks convex plane records from a shape's start index
  to the next start index, or the geometry count for the last shape. Live
  traces contained spans of 130 and 198 records. The adapter removes exact
  duplicates before checking its 63-plane limit. A synthetic 130-record
  convex reduced to six planes and retained its expected collision response.
  Larger sets are reduced to 63 original half-spaces using angular coverage.
  The adapter rejects an unbounded reduction. On synthetic 130- and 198-plane
  prisms, the maximum sampled radial expansion was 0.4623% and 0.4556%
  respectively. A GPU particle contacted the reduced 130-plane prism at the
  expected height. Those figures describe the synthetic fixtures. The
  approximation and its cost still need validation on gameplay geometry.
  Unsupported shapes are skipped while valid shapes remain active.
- In a later Fallout 4 session on the RTX 3050, the adapter submitted a
  convex with 123 input planes, all distinct, using 63 retained planes. The
  log then recorded an active simulation step and successful particle
  readback. Windows logged no Fallout 4 application failure or NVIDIA driver
  event during the observed session. This verifies the submission path in
  gameplay. Visual collision fidelity and frame time were not measured.
- One session reported `0xC0000005` on process exit after the player reported
  closing the game normally. There was no new NVIDIA event 153 and no matching
  dump. The source of that exit code remains unverified.
- The adapter implements the host-memory paths exercised here. It does not
  translate arbitrary legacy CUDA device pointers. Long sessions, other
  game versions, teardown reliability and performance under heavy force
  fields still need validation.

The original game binaries, dumps, recordings and machine-specific logs are
excluded from the repository.

## Legacy CUDA experiment

REA identified three PTX payloads targeting `sm_30` in the original core DLL.
The locally extracted payloads matched REA's SHA-256 digests. A standalone
probe loaded all three through the installed NVIDIA driver API (13050) on the
RTX 3050, bypassing the bundled CUDA 7.5 host runtime. The original
`CalculateKeyDeltas` kernel returned `2, 4, 8, 16` for adjacent XORs of the
input keys `1, 3, 7, 15, 31`. This verifies JIT loading and one kernel launch.
The same probe launched the original `CollideShapes` kernel against one
synthetic convex prism with 130 distinct planes. The particle at y=0.95
produced one contact with a +Y normal, and the particle at y=1.30 produced
zero contacts. Reducing the supplied count to 129 omitted the last plane
(the top cap), and the second particle then produced one contact. This
demonstrates that the legacy kernel used a plane beyond the FleX 1.2 convex
API limit on the RTX 3050. It does not identify the first illegal memory
access in gameplay or validate the complete legacy solver.

The probe does not include or redistribute the extracted PTX.
