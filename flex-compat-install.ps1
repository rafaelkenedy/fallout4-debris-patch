param(
    [Parameter(Mandatory)]
    [ValidateSet('Install', 'Restore')]
    [string]$Mode,
    [Parameter(Mandatory)]
    [string]$GameDirectory
)

$ErrorActionPreference = 'Stop'
$game = (Resolve-Path -LiteralPath $GameDirectory).Path
if (!(Test-Path -LiteralPath (Join-Path $game 'Fallout4.exe') -PathType Leaf)) {
    throw 'GameDirectory must contain Fallout4.exe.'
}
if (Get-Process Fallout4,Fallout4Launcher -ErrorAction SilentlyContinue) {
    throw 'Feche o Fallout 4 e seu launcher antes de alterar as DLLs.'
}

$stage = Join-Path $PSScriptRoot 'flex-compat-stage'
$backup = Join-Path $game 'flex-compat-original'
$runtimeName = 'flex-compat-runtime'
$runtime = Join-Path $game $runtimeName
$originals = [ordered]@{
    'flexRelease_x64.dll' = 'F1C8B20A685BC868EC936F7CAF3EF62805A25B7D6A26E0A5AADD28B323141B9F'
    'flexExtRelease_x64.dll' = 'C9E8D458F6E0181387ACB899EE8CD68A00695731AD262100040096FE6C48A1B6'
}

function Assert-Originals([string]$Directory) {
    foreach ($name in $originals.Keys) {
        if ((Get-FileHash -LiteralPath (Join-Path $Directory $name)).Hash -ne $originals[$name]) {
            throw "A DLL $name em $Directory não corresponde à versão verificada."
        }
    }
}

function Restore-Originals {
    Assert-Originals $backup
    foreach ($name in $originals.Keys) {
        Copy-Item -LiteralPath (Join-Path $backup $name) -Destination (Join-Path $game $name)
    }
    Assert-Originals $game
}

if ($Mode -eq 'Restore') {
    Restore-Originals
    Write-Output 'As duas DLLs originais foram restauradas e verificadas.'
    return
}

Assert-Originals $game
$runtimeSource = Join-Path $stage $runtimeName
$dependencies = @('NvFlexReleaseD3D_x64.dll','NvFlexExtReleaseD3D_x64.dll',
                  'amd_ags_x64.dll','GFSDK_Aftermath_Lib.x64.dll','nvToolsExt64_1.dll',
                  'NVIDIA-LICENSE.txt')
foreach ($name in $dependencies) {
    if (!(Test-Path -LiteralPath (Join-Path $runtimeSource $name) -PathType Leaf)) {
        throw "Dependência ausente no pacote: $name"
    }
}
foreach ($name in $originals.Keys) {
    if (!(Test-Path -LiteralPath (Join-Path $stage $name) -PathType Leaf)) {
        throw "DLL ausente no pacote: $name"
    }
}
if (!(Test-Path -LiteralPath $backup)) {
    New-Item -ItemType Directory -Path $backup | Out-Null
    foreach ($name in $originals.Keys) {
        Copy-Item -LiteralPath (Join-Path $game $name) -Destination $backup
    }
}
Assert-Originals $backup
if (!(Test-Path -LiteralPath $runtime)) {
    New-Item -ItemType Directory -Path $runtime | Out-Null
}
foreach ($name in $dependencies) {
    Copy-Item -LiteralPath (Join-Path $runtimeSource $name) -Destination $runtime
}
try {
    foreach ($name in $originals.Keys) {
        $source = Join-Path $stage $name
        $destination = Join-Path $game $name
        Copy-Item -LiteralPath $source -Destination $destination
        if ((Get-FileHash -LiteralPath $source).Hash -ne (Get-FileHash -LiteralPath $destination).Hash) {
            throw "Falha ao verificar $name depois da instalação."
        }
    }
} catch {
    Restore-Originals
    throw
}
Write-Output 'Adaptador Direct3D 11 instalado. As duas DLLs originais têm backup verificado.'
