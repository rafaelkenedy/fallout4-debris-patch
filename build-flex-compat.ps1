param(
    [string] $Compiler = 'g++',
    [string] $SdkPath = (Join-Path $PSScriptRoot 'flex-sdk-reference')
)

$ErrorActionPreference = 'Stop'
$stage = Join-Path $PSScriptRoot 'flex-compat-stage'
$runtime = Join-Path $stage 'flex-compat-runtime'
$sdk = (Resolve-Path -LiteralPath $SdkPath).Path
$include = Join-Path $sdk 'include'
$sdkDlls = Join-Path $sdk 'bin/win64'
New-Item -ItemType Directory -Path $runtime -Force | Out-Null

Push-Location -LiteralPath $PSScriptRoot
try {
    $coreSources = @('flex-legacy-bootstrap.cpp', 'flex-legacy-container.cpp',
                     'flex-legacy-asset.cpp', 'flex-legacy-mesh.cpp', 'flex-compat-core.def')
    & $Compiler -std=c++17 -O2 -shared -static -I $include @coreSources -o (Join-Path $stage 'flexRelease_x64.dll')
    if ($LASTEXITCODE -ne 0) { throw 'Core DLL compilation failed.' }

    & $Compiler -std=c++17 -O2 -shared -static flex-compat-ext.cpp flex-compat-ext.def -o (Join-Path $stage 'flexExtRelease_x64.dll')
    if ($LASTEXITCODE -ne 0) { throw 'Extension DLL compilation failed.' }
} finally {
    Pop-Location
}

foreach ($name in @('NvFlexReleaseD3D_x64.dll', 'NvFlexExtReleaseD3D_x64.dll',
                    'amd_ags_x64.dll', 'GFSDK_Aftermath_Lib.x64.dll', 'nvToolsExt64_1.dll')) {
    Copy-Item -LiteralPath (Join-Path $sdkDlls $name) -Destination $runtime -Force
}
Copy-Item -LiteralPath (Join-Path $sdk 'LICENSE.txt') -Destination (Join-Path $runtime 'NVIDIA-LICENSE.txt') -Force
Write-Output $stage
