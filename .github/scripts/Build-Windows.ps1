[CmdletBinding()]
param(
    [ValidateSet('x64')]
    [string] $Target = 'x64',
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')]
    [string] $Configuration = 'RelWithDebInfo'
)

$ErrorActionPreference = 'Stop'

if ( $DebugPreference -eq 'Continue' ) {
    $VerbosePreference = 'Continue'
    $InformationPreference = 'Continue'
}

if ( $env:CI -eq $null ) {
    throw "Build-Windows.ps1 requires CI environment"
}

if ( ! ( [System.Environment]::Is64BitOperatingSystem ) ) {
    throw "A 64-bit system is required to build the project."
}

if ( $PSVersionTable.PSVersion -lt '7.2.0' ) {
    Write-Warning 'The obs-studio PowerShell build script requires PowerShell Core 7. Install or upgrade your PowerShell version: https://aka.ms/pscore6'
    exit 2
}

# Load utility functions at script scope
$ScriptHome = $PSScriptRoot
$UtilityFunctions = Get-ChildItem -Path "$PSScriptRoot/utils.pwsh/*.ps1" -Recurse
foreach($Utility in $UtilityFunctions) {
    Write-Debug "Loading $($Utility.FullName)"
    . $Utility.FullName
}

function Install-OpenCV {
    $vcpkgRoot = if ($env:VCPKG_INSTALLATION_ROOT) { $env:VCPKG_INSTALLATION_ROOT } else { "C:\vcpkg" }
    $opencvDir = "$vcpkgRoot\installed\x64-windows\share\opencv4"
    if (-not (Test-Path -LiteralPath "$opencvDir\OpenCVConfig.cmake")) {
        Log-Group "Installing OpenCV via vcpkg (may take 15-30 min on first run)..."
        & "$vcpkgRoot\vcpkg" install "opencv4:x64-windows" --no-print-usage
        if (-not (Test-Path -LiteralPath "$opencvDir\OpenCVConfig.cmake")) {
            throw "OpenCV installation via vcpkg failed"
        }
        Log-Group
    }
    $env:OpenCV_DIR = $opencvDir
}

function Build {
    trap {
        Pop-Location -Stack BuildTemp -ErrorAction 'SilentlyContinue'
        Write-Error $_
        Log-Group
        exit 2
    }

    $ProjectRoot = Resolve-Path -Path "$PSScriptRoot/../.."

    Install-OpenCV

    Push-Location -Stack BuildTemp
    Ensure-Location $ProjectRoot

    $CmakeArgs = @('--preset', "windows-ci-${Target}")
    $CmakeBuildArgs = @('--build')
    $CmakeInstallArgs = @()

    if ( $DebugPreference -eq 'Continue' ) {
        $CmakeArgs += ('--debug-output')
        $CmakeBuildArgs += ('--verbose')
        $CmakeInstallArgs += ('--verbose')
    }

    $CmakeBuildArgs += @(
        '--preset', "windows-${Target}"
        '--config', $Configuration
        '--parallel'
        '--', '/consoleLoggerParameters:Summary', '/noLogo'
    )

    $CmakeInstallArgs += @(
        '--install', "build_${Target}"
        '--prefix', "${ProjectRoot}/release/${Configuration}"
        '--config', $Configuration
    )

    Log-Group "Configuring ${ProductName}..."
    Invoke-External cmake @CmakeArgs

    Log-Group "Building ${ProductName}..."
    Invoke-External cmake @CmakeBuildArgs

    Log-Group "Installing ${ProductName}..."
    Invoke-External cmake @CmakeInstallArgs

    Pop-Location -Stack BuildTemp
    Log-Group
}

Build
