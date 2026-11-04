param(
    [Parameter(Mandatory)][string]$PhysXSdk,
    [Parameter(Mandatory)][string]$BridgeBin,
    [string]$PatchStage = (Join-Path $PSScriptRoot 'flex-compat-stage'),
    [string]$Output = (Join-Path $PSScriptRoot 'dist')
)

$ErrorActionPreference = 'Stop'
$sdk = (Resolve-Path -LiteralPath $PhysXSdk).Path
$bridge = (Resolve-Path -LiteralPath $BridgeBin).Path
$stage = (Resolve-Path -LiteralPath $PatchStage).Path
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -version '[17.0,18.0)' -products '*' -property installationPath
if (!$vs) { throw 'Visual Studio 2022 with the C++ redistributables is required to build Setup.' }
$redistRoot = Join-Path $vs 'VC/Redist/MSVC'
$redist = Get-ChildItem -LiteralPath $redistRoot -Directory |
    Where-Object Name -Match '^14\.\d+\.\d+$' |
    Sort-Object { [version]$_.Name } -Descending |
    ForEach-Object { Join-Path $_.FullName 'x64/Microsoft.VC143.CRT' } |
    Where-Object { Test-Path -LiteralPath (Join-Path $_ 'vcruntime140_1.dll') } |
    Select-Object -First 1
if (!$redist) { throw 'Could not find the Visual Studio 2022 x64 C++ redistributable DLLs.' }

$obj = New-Item -ItemType Directory -Path (Join-Path $PSScriptRoot 'installer/obj') -Force
$scratch = New-Item -ItemType Directory -Path (Join-Path $obj.FullName ([guid]::NewGuid().ToString('N')))
$payload = Join-Path $scratch.FullName 'payload'
$runtime = New-Item -ItemType Directory -Path (Join-Path $payload 'physx-debris-runtime') -Force
$zip = Join-Path $scratch.FullName 'patch.zip'
try {
    foreach ($name in 'flexRelease_x64.dll','flexExtRelease_x64.dll') {
        Copy-Item -LiteralPath (Join-Path $stage $name) -Destination $payload
    }
    foreach ($name in 'physx-debris-bridge.dll','PhysX_64.dll','PhysXCommon_64.dll',
        'PhysXCooking_64.dll','PhysXFoundation_64.dll','PhysXGpu_64.dll') {
        Copy-Item -LiteralPath (Join-Path $bridge $name) -Destination $runtime.FullName
    }
    foreach ($name in 'msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll') {
        Copy-Item -LiteralPath (Join-Path $redist $name) -Destination $runtime.FullName
    }
    Copy-Item -LiteralPath (Join-Path $sdk 'LICENSE.md') -Destination (Join-Path $runtime.FullName 'NVIDIA-LICENSE.md')
    [IO.Compression.ZipFile]::CreateFromDirectory($payload, $zip)
    $dist = New-Item -ItemType Directory -Path $Output -Force
    Push-Location (Join-Path $PSScriptRoot 'installer')
    try {
        dotnet publish Installer.csproj -c Release -r win-x64 --self-contained true `
            -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -p:DebugType=None "-p:PayloadPath=$zip" `
            -o $dist.FullName
        if ($LASTEXITCODE) { throw "dotnet publish failed with exit code $LASTEXITCODE." }
    } finally {
        Pop-Location
    }
    $exe = Join-Path $dist.FullName 'Fallout4DebrisSetup.exe'
    $hash = (Get-FileHash -LiteralPath $exe).Hash.ToLowerInvariant()
    "$hash  Fallout4DebrisSetup.exe" | Set-Content -LiteralPath (Join-Path $dist.FullName 'Fallout4DebrisSetup.sha256')
} finally {
    $resolved = (Resolve-Path -LiteralPath $scratch.FullName).Path
    if ([IO.Path]::GetDirectoryName($resolved) -eq $obj.FullName) {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
