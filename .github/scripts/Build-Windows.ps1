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
    $opencvVersion = "4.13.0"
    $exePath = "$env:TEMP\opencv-$opencvVersion-windows.exe"
    $extractDir = "$env:TEMP\opencv-$opencvVersion"

    if (-not (Test-Path "$extractDir\opencv\build\OpenCVConfig.cmake")) {
        Log-Group "Downloading official OpenCV $opencvVersion (pre-built from opencv.org)..."
        $url = "https://github.com/opencv/opencv/releases/download/$opencvVersion/opencv-$opencvVersion-windows.exe"
        $retries = 3
        $done = $false
        while (-not $done -and $retries -gt 0) {
            try {
                Invoke-WebRequest -Uri $url -OutFile $exePath -UseBasicParsing -ErrorAction Stop
                $done = $true
            } catch {
                $retries--
                if ($retries -gt 0) {
                    Write-Warning "Download failed, retrying ($retries left)..."
                    Start-Sleep -Seconds 10
                } else {
                    throw "Failed to download OpenCV from $url"
                }
            }
        }
        Log-Group "Extracting..."
        # The exe is a 7z self-extracting archive. Use -o<dir> -y for silent extraction.
        & $exePath -o"$extractDir" -y *>$null
        if (-not (Test-Path "$extractDir\opencv\build\OpenCVConfig.cmake")) {
            throw "OpenCV extraction failed: OpenCVConfig.cmake not found"
        }
        Log-Group
    }
    $env:OpenCV_DIR = "$extractDir\opencv\build"
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
