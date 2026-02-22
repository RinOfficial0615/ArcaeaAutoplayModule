$BuildMode = "DEBUG"
$ExtraCppFlags = ""
$ExtraNdkFlags = ""
$NdkHome = $env:ANDROID_NDK_HOME
$NdkHomeSpecified = $false

function Write-LogInfo([string]$msg) {
    Write-Host "[$([char]27)[32mINFO$([char]27)[0m] $msg"
}

function Write-LogError([string]$msg) {
    Write-Host "[$([char]27)[31mERROR$([char]27)[0m] $msg"
}

function Get-NdkVersionFromText([string]$text) {
    if ([string]::IsNullOrWhiteSpace($text)) { return $null }
    $t = $text.Trim()
    $m = [regex]::Match($t, '^(?<ver>\d+(?:\.\d+){0,3})')
    if (-not $m.Success) { return $null }
    try {
        return [version]$m.Groups['ver'].Value
    } catch {
        return $null
    }
}

function Get-NdkVersionFromSourceProperties([string]$ndkDir) {
    if ([string]::IsNullOrWhiteSpace($ndkDir)) { return $null }
    $sp = Join-Path $ndkDir "source.properties"
    if (-not (Test-Path $sp)) { return $null }
    try {
        $lines = Get-Content -LiteralPath $sp -ErrorAction Stop
    } catch {
        return $null
    }

    foreach ($k in @("Pkg.BaseRevision", "Pkg.Revision")) {
        foreach ($line in $lines) {
            if ($line -match "^\s*$k\s*=\s*(.+?)\s*$") {
                $ver = Get-NdkVersionFromText $matches[1]
                if ($ver) { return $ver }
            }
        }
    }
    return $null
}

function Get-NdkVersionForDir([System.IO.DirectoryInfo]$dir) {
    if (-not $dir) { return $null }
    $ver = Get-NdkVersionFromSourceProperties $dir.FullName
    if ($ver) { return $ver }
    return Get-NdkVersionFromText $dir.Name
}

function Resolve-NdkSearchRoot([string]$inputPath) {
    if ([string]::IsNullOrWhiteSpace($inputPath)) { return $null }
    try {
        $p = (Resolve-Path -LiteralPath $inputPath -ErrorAction Stop).Path
    } catch {
        return $null
    }

    # If an actual NDK folder is provided, search its parent for siblings.
    if (Test-Path (Join-Path $p "ndk-build.cmd")) {
        return (Split-Path -Parent $p)
    }

    # If an SDK root is provided, use its ndk/ folder.
    $ndkSub = Join-Path $p "ndk"
    if (Test-Path $ndkSub) {
        return $ndkSub
    }

    # If the provided folder looks like the ndk/ root itself.
    if ((Split-Path -Leaf $p) -ieq "ndk") {
        return $p
    }

    # If the folder contains side-by-side NDK installs, treat it as the root.
    try {
        $child = Get-ChildItem -LiteralPath $p -Directory -ErrorAction SilentlyContinue |
            Where-Object { Test-Path (Join-Path $_.FullName "ndk-build.cmd") } |
            Select-Object -First 1
        if ($child) { return $p }
    } catch {
        # ignore
    }

    return $null
}

function Select-NewestNdkUnderRoot([string]$ndkRoot) {
    if ([string]::IsNullOrWhiteSpace($ndkRoot)) { return $null }
    if (-not (Test-Path $ndkRoot)) { return $null }

    $candidates = @()
    foreach ($d in (Get-ChildItem -LiteralPath $ndkRoot -Directory -ErrorAction SilentlyContinue)) {
        if (-not (Test-Path (Join-Path $d.FullName "ndk-build.cmd"))) { continue }
        $ver = Get-NdkVersionForDir $d
        if (-not $ver) { continue }
        $candidates += [pscustomobject]@{
            Dir = $d
            Version = $ver
        }
    }
    if ($candidates.Count -eq 0) { return $null }
    return ($candidates | Sort-Object -Property Version -Descending | Select-Object -First 1)
}

function Show-Help {
    @"
Usage: .\build.ps1 [options]
Build ArcAutoplayModule using Android NDK

Options:
    --rel                       Build in RELEASE mode
    --rebuild                   Rebuild the project (clean build)
    --cpp-build-flags <flags>   Additional compiler flags
    --ndk-build-flags <flags>   Additional ndk-build flags
    --ndk-home <path>           Use a specific Android NDK path
    --help                      Show this help message
"@
    exit 0
}

$i = 0
while ($i -lt $args.Length) {
    switch ($args[$i]) {
        "--rel" {
            $BuildMode = "RELEASE"
            $i++
        }
        "--rebuild" {
            if (Test-Path "build") { Remove-Item -Recurse -Force "build" }
            $ExtraNdkFlags += " -B "
            $i++
        }
        "--cpp-build-flags" {
            $ExtraCppFlags += " $($args[$i+1]) "
            $i += 2
        }
        "--ndk-build-flags" {
            $ExtraNdkFlags += " $($args[$i+1]) "
            $i += 2
        }
        "--ndk-home" {
            $NdkHome = $args[$i+1]
            $NdkHomeSpecified = $true
            $i += 2
        }
        "--help" {
            Show-Help
        }
        Default {
            Write-LogError "Unknown option: $($args[$i])"
            Show-Help
            exit 1
        }
    }
}

# Ensure relative paths resolve from this repo.
Set-Location -LiteralPath $PSScriptRoot

if (-not (Test-Path "build")) {
    New-Item -ItemType Directory -Path "build" | Out-Null
}

Write-LogInfo "Building ArcAutoplayModule in $([char]27)[36m$BuildMode$([char]27)[0m mode"

if ([string]::IsNullOrWhiteSpace($NdkHome)) {
    $SdkRoot = $env:ANDROID_SDK_ROOT
    if ([string]::IsNullOrWhiteSpace($SdkRoot)) {
        $SdkRoot = $env:ANDROID_HOME
    }
    if (-not [string]::IsNullOrWhiteSpace($SdkRoot)) {
        $NdkHome = Join-Path $SdkRoot "ndk"
    }
}

if ([string]::IsNullOrWhiteSpace($NdkHome)) {
    Write-LogError "ANDROID_NDK_HOME is not set (or use --ndk-home)"
    exit 1
}

$SearchRoot = Resolve-NdkSearchRoot $NdkHome
if (-not $SearchRoot) {
    Write-LogError "Unable to locate NDK installs from path: $NdkHome"
    exit 1
}

$Newest = Select-NewestNdkUnderRoot $SearchRoot
if (-not $Newest) {
    Write-LogError "No versioned NDK installs found under: $SearchRoot"
    exit 1
}

if ($Newest.Version.Major -le 28) {
    Write-LogError "Newest installed NDK is $($Newest.Version) (<= r28). Install r28+ under: $SearchRoot"
    exit 1
}

if (-not $NdkHomeSpecified) {
    $NdkHome = $Newest.Dir.FullName
} else {
    # If user gave a root-like path, still select the newest NDK under it.
    if (-not (Test-Path (Join-Path $NdkHome "ndk-build.cmd"))) {
        $NdkHome = $Newest.Dir.FullName
    } else {
        try { $NdkHome = (Resolve-Path -LiteralPath $NdkHome -ErrorAction Stop).Path } catch { }
    }
}

$UsingVer = Get-NdkVersionFromSourceProperties $NdkHome
if (-not $UsingVer) {
    $UsingVer = Get-NdkVersionFromText (Split-Path -Leaf $NdkHome)
}
if (-not $UsingVer) {
    Write-LogError "Failed to parse NDK version at: $NdkHome"
    exit 1
}
if ($UsingVer.Major -le 28) {
    Write-LogError "Selected NDK is $UsingVer (<= r28). Install r28+ and/or update ANDROID_NDK_HOME."
    exit 1
}

Write-LogInfo "Using Android NDK: $NdkHome ($UsingVer)"

$NdkBuildCmd = Join-Path $NdkHome "ndk-build.cmd"
if (-not (Test-Path $NdkBuildCmd)) {
    Write-LogError "ndk-build.cmd not found: $NdkBuildCmd"
    exit 1
}

$ArgList = @(
    "NDK_PROJECT_PATH=$PSScriptRoot",
    "APP_BUILD_SCRIPT=$PSScriptRoot\Android.mk",
    "NDK_APPLICATION_MK=$PSScriptRoot\Application.mk",
    "NDK_OUT=$PSScriptRoot\build\obj",
    "NDK_LIBS_OUT=$PSScriptRoot\build\libs",
    "-j$env:NUMBER_OF_PROCESSORS",
    $(if ($BuildMode -eq "DEBUG") { "NDK_DEBUG=1" } else { "NDK_DEBUG=0" })
)

if ($ExtraNdkFlags -ne "") { $ArgList += $ExtraNdkFlags.Trim() }
if ($ExtraCppFlags -ne "") { $ArgList += "APP_CPPFLAGS+=$($ExtraCppFlags.Trim())" }

Write-LogInfo "NDK build command: $NdkBuildCmd $ArgList"

& $NdkBuildCmd @ArgList > "build/build.log" 2>&1

if ($LASTEXITCODE -ne 0) {
    Write-LogError "Build failed with exit code $LASTEXITCODE"
    Write-LogError "Check build/build.log for details"

    Write-Host "`n[33m=== Last 60 lines of build log ===[0m" -ForegroundColor Yellow
    if (Test-Path "build/build.log") {
        Get-Content "build/build.log" -Tail 60
    }
    Write-Host "[33m===============================[0m" -ForegroundColor Yellow
    exit 1
}

if ($BuildMode -eq "RELEASE") {
    Write-LogInfo "Stripping libraries for release"
    $Stripper = Join-Path $NdkHome "toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe"
    if (Test-Path $Stripper) {
        & $Stripper -s build/libs/arm64-v8a/libarc_autoplay.so
    }
}

$TmpDir = "build/module_tmp"
if (Test-Path $TmpDir) { Remove-Item -Recurse -Force $TmpDir }
New-Item -ItemType Directory -Path "$TmpDir/zygisk" | Out-Null

Copy-Item "build/libs/arm64-v8a/libarc_autoplay.so" -Destination "$TmpDir/zygisk/arm64-v8a.so"
Copy-Item "module.prop" -Destination "$TmpDir/"

$ZipPath = "build/ArcAutoplayModule.zip"
if (Test-Path $ZipPath) { Remove-Item $ZipPath }
Compress-Archive -Path "$TmpDir\*" -DestinationPath $ZipPath

Remove-Item -Recurse -Force $TmpDir
Write-LogInfo "OK!"
