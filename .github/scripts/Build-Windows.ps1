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
        $null = New-Item -ItemType Directory -Path $extractDir -Force
        $7z = Get-Command "7z" -ErrorAction SilentlyContinue
        if ($7z) {
            # 7z is pre-installed on GitHub Actions Windows runners; handles
            # the 7z SFX archive reliably without GUI popups.
            & $7z x "$exePath" "-o$extractDir" -y 2>&1 | Write-Host
            if ($LASTEXITCODE -ne 0) {
                throw "7z extraction of OpenCV archive failed with exit code $LASTEXITCODE"
            }
        } else {
            # Fallback: run the SFX exe directly in the target directory.
            Push-Location $extractDir
            try {
                $output = & $exePath -y 2>&1
                if ($LASTEXITCODE -ne 0) {
                    Write-Host $output
                    throw "OpenCV self-extracting archive failed with exit code $LASTEXITCODE"
                }
            } finally {
                Pop-Location
            }
        }
        if (-not (Test-Path "$extractDir\opencv\build\OpenCVConfig.cmake")) {
            # Try to locate it elsewhere in case the archive layout differs
            $found = Get-ChildItem -Path $extractDir -Recurse -Filter "OpenCVConfig.cmake" -Depth 5 -ErrorAction SilentlyContinue | Select-Object -First 1
            if (-not $found) {
                Get-ChildItem -Path $extractDir -Depth 2 | Select-Object FullName | Write-Host
                throw "OpenCV extraction failed: OpenCVConfig.cmake not found under $extractDir"
            }
            Write-Host "Found OpenCVConfig.cmake at $($found.FullName), adjusting OpenCV_DIR..."
            $env:OpenCV_DIR = $found.Directory.FullName
            return
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
