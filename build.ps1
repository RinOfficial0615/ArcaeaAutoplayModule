$ErrorActionPreference = "Stop"

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
Build ArcHelperModule using Android NDK

Options:
    --rel                       Build in RELEASE mode
    --rebuild                   Rebuild the project (clean build)
    --cpp-build-flags <flags>   Additional compiler flags
    --ndk-build-flags <flags>   Additional ndk-build flags
    --ndk-home <path>           Use a specific Android NDK path
    --help                      Show this help message

Notes:
    Switching between DEBUG and --rel discards build/obj automatically.
    APP_CPPFLAGS uses -flto, so objects hold LLVM bitcode rather than machine
    code and ndk-build cannot detect the optimisation change; reusing them
    silently produces a half-optimised library. See docs/cpp/build-pitfalls.md.
"@
}

$i = 0
while ($i -lt $args.Length) {
    switch ($args[$i]) {
        "--rel" {
            $BuildMode = "RELEASE"
            $i++
        }
        "--rebuild" {
            $BuildDir = Join-Path $PSScriptRoot "build"
            if (Test-Path $BuildDir) { Remove-Item -LiteralPath $BuildDir -Recurse -Force }
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
            exit 0
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

# Keep the pinned LSPlt submodule clean. ArcHelper's live-PLT compatibility
# patch is applied to a disposable build copy so a clean clone is reproducible.
$LspltSourceDir = Join-Path $PSScriptRoot "third_party/lsplt/lsplt"
$LspltStageRoot = Join-Path $PSScriptRoot "build/generated/lsplt"
$LspltPatch = Join-Path $PSScriptRoot "patches/lsplt-live-plt.patch"
if (-not (Test-Path $LspltSourceDir) -or -not (Test-Path $LspltPatch)) {
    Write-LogError "LSPlt submodule or ArcHelper patch is missing"
    exit 1
}
if (Test-Path $LspltStageRoot) {
    Remove-Item -LiteralPath $LspltStageRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $LspltStageRoot -Force | Out-Null
Copy-Item -LiteralPath $LspltSourceDir -Destination $LspltStageRoot -Recurse -Force
# The disposable copy keeps the submodule's LF blobs. Disable autocrlf and
# ignore whitespace so a CRLF-checked-out patch still applies on Windows.
& git -C $PSScriptRoot -c core.autocrlf=false apply --check --ignore-whitespace --directory=build/generated/lsplt $LspltPatch
if ($LASTEXITCODE -ne 0) {
    Write-LogError "LSPlt compatibility patch no longer applies to the pinned submodule"
    exit 1
}
& git -C $PSScriptRoot -c core.autocrlf=false apply --ignore-whitespace --directory=build/generated/lsplt $LspltPatch
if ($LASTEXITCODE -ne 0) {
    Write-LogError "Failed to stage the LSPlt compatibility patch"
    exit 1
}

Write-LogInfo "Building ArcHelperModule in $([char]27)[36m$BuildMode$([char]27)[0m mode"

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
    Write-LogError "Newest installed NDK is $($Newest.Version) (<= r28). Install r29+ under: $SearchRoot"
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
    Write-LogError "Selected NDK is $UsingVer (<= r28). Install r29+ and/or update ANDROID_NDK_HOME."
    exit 1
}

Write-LogInfo "Using Android NDK: $NdkHome ($UsingVer)"

$NdkBuildCmd = Join-Path $NdkHome "ndk-build.cmd"
if (-not (Test-Path $NdkBuildCmd)) {
    Write-LogError "ndk-build.cmd not found: $NdkBuildCmd"
    exit 1
}

# ndk-build cannot see that the optimisation level changed. APP_CPPFLAGS carries
# -flto, so every .o holds LLVM bitcode instead of machine code and the dependency
# check only has timestamps to go on. Switching DEBUG -> RELEASE therefore reuses
# -O0 objects and links a half-optimised .so: measured 1,512,696 bytes against
# 1,261,784 for a clean build (+19.9%), with nothing anywhere reporting a problem.
# Detect the mode flip here and drop the objects before ndk-build sees them.
$ModeStampPath = Join-Path $PSScriptRoot "build/.last-build-mode"
$ObjDir = Join-Path $PSScriptRoot "build/obj"
$PreviousMode = $null
if (Test-Path $ModeStampPath) {
    $PreviousMode = (Get-Content -LiteralPath $ModeStampPath -ErrorAction SilentlyContinue |
        Select-Object -First 1)
    if ($PreviousMode) { $PreviousMode = $PreviousMode.Trim() }
}
if ($PreviousMode -and $PreviousMode -ne $BuildMode -and (Test-Path $ObjDir)) {
    Write-LogInfo "Build mode changed $PreviousMode -> $BuildMode; discarding stale objects"
    try {
        Remove-Item -LiteralPath $ObjDir -Recurse -Force -ErrorAction Stop
    } catch {
        Write-LogError "Could not remove $ObjDir : $($_.Exception.Message)"
        Write-LogError "Delete it manually and re-run. Building anyway would link"
        Write-LogError "$PreviousMode objects into a $BuildMode library."
        exit 1
    }
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

    Write-Host "`n$([char]27)[33m=== Last 60 lines of build log ===$([char]27)[0m" -ForegroundColor Yellow
    if (Test-Path "build/build.log") {
        Get-Content "build/build.log" -Tail 60
    }
    Write-Host "$([char]27)[33m===============================$([char]27)[0m" -ForegroundColor Yellow
    exit 1
}

# Stamped as soon as the objects are known-good, not at the very end: the cleanup
# of build/module_tmp is the last step and can fail for reasons unrelated to the
# build (locked files, permission), which would otherwise leave the stamp missing
# and defeat the mode-flip check above on the next run.
Set-Content -LiteralPath $ModeStampPath -Value $BuildMode

if ($BuildMode -eq "RELEASE") {
    Write-LogInfo "Stripping libraries for release"
    $Stripper = Join-Path $NdkHome "toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe"
    if (-not (Test-Path $Stripper)) {
        throw "llvm-strip not found: $Stripper"
    }
    & $Stripper -s build/libs/arm64-v8a/libarc_helper.so
    if ($LASTEXITCODE -ne 0) {
        throw "llvm-strip failed with exit code $LASTEXITCODE"
    }
}

$TmpDir = "build/module_tmp"
if (Test-Path $TmpDir) { Remove-Item -Recurse -Force $TmpDir }
New-Item -ItemType Directory -Path "$TmpDir/zygisk" | Out-Null

Copy-Item "build/libs/arm64-v8a/libarc_helper.so" -Destination "$TmpDir/zygisk/arm64-v8a.so"
Copy-Item "build/libs/arm64-v8a/libshadowhook_nothing.so" -Destination "$TmpDir/zygisk/libshadowhook_nothing.so"
Copy-Item "module/module.prop" -Destination "$TmpDir/"
Copy-Item "module/scope.txt" -Destination "$TmpDir/"

$ZipPath = "build/ArcHelperModule.zip"
if (Test-Path $ZipPath) { Remove-Item $ZipPath }
Compress-Archive -Path "$TmpDir\*" -DestinationPath $ZipPath

Add-Type -AssemblyName System.IO.Compression.FileSystem
$Archive = [System.IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $ZipPath).Path)
try {
    $ActualEntries = @($Archive.Entries | ForEach-Object { $_.FullName.Replace('\', '/') } | Sort-Object)
    $ExpectedEntries = @(
        "module.prop",
        "scope.txt",
        "zygisk/arm64-v8a.so",
        "zygisk/libshadowhook_nothing.so"
    ) | Sort-Object
    if (($ActualEntries -join "`n") -ne ($ExpectedEntries -join "`n")) {
        throw "Unexpected module contents: $($ActualEntries -join ', ')"
    }
} finally {
    $Archive.Dispose()
}

Remove-Item -Recurse -Force $TmpDir
Write-LogInfo "OK!"
