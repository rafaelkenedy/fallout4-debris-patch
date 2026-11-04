param([string]$Objdump = 'objdump')

$ErrorActionPreference = 'Stop'
$stage = Join-Path $PSScriptRoot 'flex-compat-stage'
$core = @(
    'flexGetParticles','flexSetParticles','flexGetActiveCount','flexSetActive',
    'flexSetParams','flexUpdateSolver','flexDestroySolver','flexCreateSolver',
    'flexFree','flexSetVelocities','flexSnapshot','flexStopRecord','flexStartRecord',
    'flexAcquireContext','flexShutdown','flexInit','flexSetShapes',
    'flexDestroyTriangleMesh','flexCreateTriangleMesh','flexUpdateTriangleMesh',
    'flexGetVelocities','flexSetPhases','flexSetRigids','flexGetRigidTransforms',
    'flexGetBounds','flexAlloc','flexSetFence','flexWaitFence'
)
$extension = @(
    'flexExtGetParticleData','flexExtDestroyContainer','flexExtCreateContainer',
    'flexExtDestroyAsset','flexExtCreateRigidFromMesh','flexExtSetForceFields'
)

function Assert-Exports([string]$Name, [string[]]$Required, [bool]$Forwarded) {
    $dump = & $Objdump -p (Join-Path $stage $Name) | Out-String
    if ($LASTEXITCODE -ne 0) { throw "Could not inspect $Name." }
    if ($dump -notmatch 'file format pei-x86-64') { throw "$Name is not a Windows x64 binary." }
    foreach ($entry in $Required) {
        if ($dump -notmatch "(?m)^\s*\[\s*\d+\]\s+(?:\+base\[\s*\d+\]\s+[a-f0-9]+\s+)?$entry\s*$") {
            throw "$Name is missing $entry."
        }
        if ($Forwarded -and $dump -notmatch "Forwarder RVA -- flexRelease_x64\.$entry\b") {
            throw "$Name has an incorrect forwarder for $entry."
        }
    }
    if ($dump -match '(?im)DLL Name:\s*cudart') { throw "$Name imports a legacy CUDA runtime." }
}

Assert-Exports 'flexRelease_x64.dll' ($core + $extension) $false
Assert-Exports 'flexExtRelease_x64.dll' $extension $true
Write-Output 'Verified Windows x64 exports for all 34 Fallout 4 imports and all six extension forwarders.'
