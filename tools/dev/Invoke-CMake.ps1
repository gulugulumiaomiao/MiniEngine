param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $CMakeArguments
)

$ErrorActionPreference = 'Stop'

function Find-Executable {
    param(
        [string] $Command,
        [string[]] $Candidates
    )

    foreach ($candidate in $Candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    $resolved = Get-Command $Command -ErrorAction SilentlyContinue
    if ($resolved) {
        return $resolved.Source
    }
    return $null
}

function Find-WinGetPackageBin {
    param(
        [string] $Prefix,
        [string] $Executable
    )

    $packagesRoot = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages'
    if (-not (Test-Path -LiteralPath $packagesRoot -PathType Container)) {
        return $null
    }

    $packages = Get-ChildItem -LiteralPath $packagesRoot -Directory |
        Where-Object { $_.Name.StartsWith($Prefix) } |
        Sort-Object LastWriteTime -Descending
    foreach ($package in $packages) {
        $match = Get-ChildItem -LiteralPath $package.FullName -Filter $Executable `
            -File -Recurse -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($match) {
            return $match.DirectoryName
        }
    }
    return $null
}

$winLibsBin = Find-WinGetPackageBin `
    'BrechtSanders.WinLibs.POSIX.UCRT.LLVM_' 'clang++.exe'
$ninjaBin = Find-WinGetPackageBin 'Ninja-build.Ninja_' 'ninja.exe'

$cmake = Find-Executable 'cmake.exe' @(
    (Join-Path $env:ProgramFiles 'CMake\bin\cmake.exe'),
    $(if ($winLibsBin) { Join-Path $winLibsBin 'cmake.exe' })
)
if (-not $cmake) {
    Write-Error 'CMake not found. Run tools/dev/Setup-Windows.ps1 first.'
}

$clang = Find-Executable 'clang++.exe' @(
    $(if ($winLibsBin) { Join-Path $winLibsBin 'clang++.exe' }),
    'C:\mingw64\bin\clang++.exe'
)
if (-not $clang) {
    Write-Error 'WinLibs Clang not found. Run tools/dev/Setup-Windows.ps1 first.'
}
$toolchainBin = Split-Path -Parent $clang

$ninja = Find-Executable 'ninja.exe' @(
    $(if ($ninjaBin) { Join-Path $ninjaBin 'ninja.exe' }),
    (Join-Path $toolchainBin 'ninja.exe')
)
if (-not $ninja) {
    Write-Error 'Ninja not found. Run tools/dev/Setup-Windows.ps1 first.'
}

$vulkanRoot = $env:VULKAN_SDK
if (-not $vulkanRoot -or
    -not (Test-Path -LiteralPath (Join-Path $vulkanRoot 'Bin\glslc.exe'))) {
    $sdkRoot = 'C:\VulkanSDK'
    if (Test-Path -LiteralPath $sdkRoot -PathType Container) {
        $sdk = Get-ChildItem -LiteralPath $sdkRoot -Directory |
            Sort-Object Name -Descending |
            Where-Object {
                Test-Path -LiteralPath (Join-Path $_.FullName 'Bin\glslc.exe')
            } |
            Select-Object -First 1
        if ($sdk) {
            $vulkanRoot = $sdk.FullName
        }
    }
}
if (-not $vulkanRoot) {
    Write-Error 'Vulkan SDK not found. Run tools/dev/Setup-Windows.ps1 first.'
}

$env:VULKAN_SDK = $vulkanRoot
$requiredBins = @(
    $toolchainBin,
    (Split-Path -Parent $ninja),
    (Split-Path -Parent $cmake),
    (Join-Path $vulkanRoot 'Bin')
)
$env:Path = (($requiredBins + @($env:Path)) -join ';')

& $cmake @CMakeArguments
exit $LASTEXITCODE
