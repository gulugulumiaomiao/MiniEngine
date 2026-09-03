$ErrorActionPreference = 'Stop'

if (-not (Get-Command winget.exe -ErrorAction SilentlyContinue)) {
    Write-Error 'winget is required. Install or update Microsoft App Installer first.'
}

$packages = @(
    'Kitware.CMake',
    'Ninja-build.Ninja',
    'BrechtSanders.WinLibs.POSIX.UCRT.LLVM',
    'KhronosGroup.VulkanSDK'
)

foreach ($package in $packages) {
    winget install --id $package --exact --silent `
        --accept-package-agreements --accept-source-agreements
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Failed to install package: $package"
    }
}

$code = Get-Command code.cmd -ErrorAction SilentlyContinue
if ($code) {
    $extensions = @(
        'llvm-vs-code-extensions.vscode-clangd',
        'ms-vscode.cmake-tools',
        'vadimcn.vscode-lldb'
    )
    foreach ($extension in $extensions) {
        & $code.Source --install-extension $extension
    }
} else {
    Write-Warning 'VS Code command line was not found; install recommended extensions when VS Code prompts.'
}

Write-Host 'Setup complete. Restart VS Code, open the repository, and press F5.'
