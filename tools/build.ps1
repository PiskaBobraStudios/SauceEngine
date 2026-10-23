param([switch]$NoRun, [switch]$MappedBuild, [string]$OriginalProjectRoot)
$ErrorActionPreference = 'Stop'
$ScriptProjectRoot = Split-Path -Parent $PSScriptRoot

if (-not $MappedBuild -and $ScriptProjectRoot -match '[^\x00-\x7F]') {
    $SubstPath = Join-Path $env:SystemRoot 'System32\subst.exe'
    foreach ($DriveLetter in @('Z', 'Y', 'X', 'W', 'V', 'U', 'T', 'S', 'R', 'Q', 'P', 'O', 'N', 'M', 'L', 'K', 'J', 'I', 'H', 'G', 'F', 'E', 'D', 'C', 'B', 'A')) {
        $DriveName = $DriveLetter + ':'
        if (Test-Path -LiteralPath ($DriveName + '\')) { continue }
        & $SubstPath $DriveName $ScriptProjectRoot 2>$null
        if ($LASTEXITCODE -ne 0) { continue }
        $MappedRoot = $DriveName + '\'
        $MappedScript = Join-Path $MappedRoot 'tools\build.ps1'
        if (-not (Test-Path -LiteralPath $MappedScript -PathType Leaf)) {
            & $SubstPath /D $DriveName 2>$null
            continue
        }
        $PowerShellPath = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
        $PowerShellArguments = @('-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $MappedScript, '-MappedBuild', '-OriginalProjectRoot', $ScriptProjectRoot)
        if ($NoRun) { $PowerShellArguments += '-NoRun' }
        & $PowerShellPath @PowerShellArguments
        $BuildResult = $LASTEXITCODE
        & $SubstPath /D $DriveName 2>$null
        exit $BuildResult
    }
    Write-Host 'Warning: unable to create an ASCII drive alias; trying the original project path.'
}

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$LaunchRoot = if ([string]::IsNullOrWhiteSpace($OriginalProjectRoot)) { $ProjectRoot } else { $OriginalProjectRoot }
$BuildDirectory = Join-Path $ProjectRoot 'build'
$OutputExecutable = Join-Path $ProjectRoot 'SauceEngine.exe'
$SystemDrive = $env:SystemDrive
if ([string]::IsNullOrWhiteSpace($SystemDrive)) { $SystemDrive = 'C:' }
$SystemRoot = $SystemDrive
if (-not $SystemRoot.EndsWith('\')) { $SystemRoot += '\' }
$SearchRoots = New-Object 'System.Collections.Generic.List[string]'
$BaseRoots = @(
    (Join-Path $SystemRoot 'msys64'),
    (Join-Path $SystemRoot 'mingw64'),
    (Join-Path $SystemRoot 'mingw'),
    (Join-Path $SystemRoot 'tools'),
    (Join-Path $SystemRoot 'Dev-Cpp'),
    (Join-Path $SystemRoot 'TDM-GCC-64'),
    (Join-Path $SystemRoot 'ProgramData\chocolatey'),
    $env:ProgramFiles,
    ${env:ProgramFiles(x86)},
    $env:ProgramData,
    (Join-Path $env:LOCALAPPDATA 'Programs'),
    (Join-Path $env:USERPROFILE 'scoop\apps'),
    (Join-Path $env:USERPROFILE 'tools'),
    (Join-Path $env:USERPROFILE 'Downloads'),
    $env:ChocolateyInstall
)
foreach ($Root in $BaseRoots) {
    if (-not [string]::IsNullOrWhiteSpace($Root) -and (Test-Path -LiteralPath $Root -PathType Container)) {
        $ResolvedRoot = (Resolve-Path -LiteralPath $Root).Path
        if (-not $SearchRoots.Contains($ResolvedRoot)) { $SearchRoots.Add($ResolvedRoot) }
    }
}
$DirectDirectories = New-Object 'System.Collections.Generic.List[string]'
foreach ($Directory in @(
    (Join-Path $SystemRoot 'msys64\ucrt64\bin'),
    (Join-Path $SystemRoot 'msys64\mingw64\bin'),
    (Join-Path $SystemRoot 'msys64\clang64\bin'),
    (Join-Path $SystemRoot 'mingw64\bin'),
    (Join-Path $SystemRoot 'mingw\bin'),
    (Join-Path $SystemRoot 'MinGW\bin'),
    (Join-Path $SystemRoot 'TDM-GCC-64\bin'),
    (Join-Path $SystemRoot 'tools\mingw64\bin'),
    (Join-Path $env:ProgramFiles 'CMake\bin'),
    (Join-Path ${env:ProgramFiles(x86)} 'CMake\bin'),
    (Join-Path $env:LOCALAPPDATA 'Programs\CMake\bin'),
    (Join-Path $env:LOCALAPPDATA 'Programs\msys64\ucrt64\bin'),
    (Join-Path $env:USERPROFILE 'scoop\apps\mingw\current\bin'),
    (Join-Path $env:USERPROFILE 'scoop\apps\gcc\current\bin'),
    (Join-Path $env:USERPROFILE 'scoop\apps\cmake\current\bin'),
    $env:ChocolateyInstall,
    $env:MINGW_HOME,
    $env:MINGW_ROOT,
    $env:MSYS2_ROOT,
    $env:MSYS2_PATH,
    $env:GCC_HOME
)) {
    if (-not [string]::IsNullOrWhiteSpace($Directory)) {
        if (Test-Path -LiteralPath $Directory -PathType Container) {
            $ResolvedDirectory = (Resolve-Path -LiteralPath $Directory).Path
            if (-not $DirectDirectories.Contains($ResolvedDirectory)) { $DirectDirectories.Add($ResolvedDirectory) }
        }
        foreach ($Suffix in @('bin', 'ucrt64\bin', 'mingw64\bin', 'clang64\bin')) {
            $NestedDirectory = Join-Path $Directory $Suffix
            if (Test-Path -LiteralPath $NestedDirectory -PathType Container) {
                $ResolvedNestedDirectory = (Resolve-Path -LiteralPath $NestedDirectory).Path
                if (-not $DirectDirectories.Contains($ResolvedNestedDirectory)) { $DirectDirectories.Add($ResolvedNestedDirectory) }
            }
        }
    }
}
function Test-ExecutableCandidate {
    param([string]$Path, [string]$Name)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $false }
    if ($Name -ne 'g++.exe') { return $true }
    try {
        $Target = (& $Path -dumpmachine 2>$null | Select-Object -First 1)
        $ExitCode = $LASTEXITCODE
        return ($ExitCode -eq 0 -and ([string]$Target -match '(?i)x86_64.*(mingw|w64)|(mingw|w64).*x86_64'))
    } catch {
        return $false
    }
}
function Find-Executable {
    param([string]$Name, [switch]$FullScan)
    $Checked = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
    $Candidates = New-Object 'System.Collections.Generic.List[string]'
    foreach ($Command in @(Get-Command -Name $Name -All -ErrorAction SilentlyContinue)) {
        $CommandPath = $Command.Source
        if ([string]::IsNullOrWhiteSpace($CommandPath)) { $CommandPath = $Command.Path }
        if (-not [string]::IsNullOrWhiteSpace($CommandPath)) { $Candidates.Add($CommandPath) }
    }
    try {
        foreach ($FoundPath in @(& where.exe $Name 2>$null)) {
            if (-not [string]::IsNullOrWhiteSpace($FoundPath)) { $Candidates.Add($FoundPath.Trim()) }
        }
    } catch {}
    foreach ($Directory in $DirectDirectories) {
        $CandidatePath = Join-Path $Directory $Name
        if (Test-Path -LiteralPath $CandidatePath -PathType Leaf) { $Candidates.Add($CandidatePath) }
    }
    foreach ($Candidate in $Candidates) {
        if ($Checked.Add($Candidate) -and (Test-ExecutableCandidate -Path $Candidate -Name $Name)) { return $Candidate }
    }
    foreach ($Root in $SearchRoots) {
        Write-Host "Searching $Name in $Root"
        try {
            foreach ($File in Get-ChildItem -LiteralPath $Root -Filter $Name -File -Recurse -Force -ErrorAction SilentlyContinue) {
                if ($Checked.Add($File.FullName) -and (Test-ExecutableCandidate -Path $File.FullName -Name $Name)) { return $File.FullName }
            }
        } catch {}
    }
    if (-not $FullScan) { return $null }
    $SkipDirectories = @('Windows', 'System Volume Information', '$Recycle.Bin', 'Windows.old', '$WINDOWS.~BT', 'Recovery', 'WinSxS', 'node_modules', '.git', '.cache')
    foreach ($Drive in [System.IO.DriveInfo]::GetDrives()) {
        if (-not $Drive.IsReady -or $Drive.DriveType -ne [System.IO.DriveType]::Fixed) { continue }
        Write-Host "Searching remaining files on $($Drive.RootDirectory.FullName) for $Name"
        $Stack = New-Object 'System.Collections.Generic.Stack[string]'
        $Stack.Push($Drive.RootDirectory.FullName)
        while ($Stack.Count -gt 0) {
            $CurrentDirectory = $Stack.Pop()
            try {
                foreach ($FilePath in [System.IO.Directory]::GetFiles($CurrentDirectory, $Name, [System.IO.SearchOption]::TopDirectoryOnly)) {
                    if ($Checked.Add($FilePath) -and (Test-ExecutableCandidate -Path $FilePath -Name $Name)) { return $FilePath }
                }
            } catch {}
            try {
                $Children = [System.IO.Directory]::GetDirectories($CurrentDirectory)
            } catch {
                continue
            }
            foreach ($Child in $Children) {
                $TrimmedChild = $Child.TrimEnd([char[]]@([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar))
                $Leaf = [System.IO.Path]::GetFileName($TrimmedChild)
                if ($SkipDirectories -contains $Leaf) { continue }
                try {
                    $Attributes = [System.IO.File]::GetAttributes($Child)
                    if (($Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) { continue }
                } catch {
                    continue
                }
                $Stack.Push($Child)
            }
        }
    }
    return $null
}
function Install-Msys2 {
    $Winget = Get-Command -Name 'winget.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $Winget) { return $false }
    $Answer = Read-Host 'MinGW-w64 was not found. Install MSYS2 and the x64 compiler with winget now? [Y/n]'
    if ($Answer -match '^(?i:n|no)$') { return $false }
    & $Winget.Source install --id MSYS2.MSYS2 -e --accept-package-agreements --accept-source-agreements
    if ($LASTEXITCODE -ne 0) { return $false }
    $BashCandidates = @(
        (Join-Path $SystemRoot 'msys64\usr\bin\bash.exe'),
        (Join-Path $env:LOCALAPPDATA 'Programs\msys64\usr\bin\bash.exe'),
        (Join-Path $env:USERPROFILE 'msys64\usr\bin\bash.exe'),
        (Join-Path $env:ProgramFiles 'MSYS2\usr\bin\bash.exe')
    )
    $BashPath = $null
    foreach ($Candidate in $BashCandidates) {
        if (Test-Path -LiteralPath $Candidate -PathType Leaf) { $BashPath = $Candidate; break }
    }
    if (-not $BashPath) { $BashPath = Find-Executable -Name 'bash.exe' -FullScan }
    if (-not $BashPath) { return $false }
    & $BashPath -lc 'pacman -Sy --noconfirm && pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make'
    if ($LASTEXITCODE -ne 0) { return $false }
    $MsysRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $BashPath))
    $env:PATH = "$(Join-Path $MsysRoot 'ucrt64\bin');$(Join-Path $MsysRoot 'usr\bin');$env:PATH"
    return $true
}
function Install-CMake {
    $Winget = Get-Command -Name 'winget.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $Winget) { return $false }
    $Answer = Read-Host 'CMake was not found. Install CMake with winget now? [Y/n]'
    if ($Answer -match '^(?i:n|no)$') { return $false }
    & $Winget.Source install --id Kitware.CMake -e --accept-package-agreements --accept-source-agreements
    return ($LASTEXITCODE -eq 0)
}
Write-Host 'Searching for a 64-bit MinGW-w64 compiler.'
$CompilerPath = Find-Executable -Name 'g++.exe' -FullScan
if (-not $CompilerPath -and (Install-Msys2)) {
    $MsysBins = @(
        (Join-Path $SystemRoot 'msys64\ucrt64\bin'),
        (Join-Path $SystemRoot 'msys64\mingw64\bin'),
        (Join-Path $env:LOCALAPPDATA 'Programs\msys64\ucrt64\bin'),
        (Join-Path $env:USERPROFILE 'msys64\ucrt64\bin')
    )
    foreach ($Directory in $MsysBins) {
        if (Test-Path -LiteralPath $Directory -PathType Container) {
            $env:PATH = "$Directory;$env:PATH"
        }
    }
    $CompilerPath = Find-Executable -Name 'g++.exe' -FullScan
}
if (-not $CompilerPath) {
    Write-Host 'No compatible 64-bit MinGW-w64 compiler was found.'
    exit 1
}
$CompilerDirectory = Split-Path -Parent $CompilerPath
$env:PATH = "$CompilerDirectory;$env:PATH"
$CompilerParent = Split-Path -Parent $CompilerDirectory
$MsysUsrBin = Join-Path (Split-Path -Parent $CompilerParent) 'usr\bin'
if (Test-Path -LiteralPath $MsysUsrBin -PathType Container) { $env:PATH = "$MsysUsrBin;$env:PATH" }
$ResourceCompilerPath = Join-Path $CompilerDirectory 'windres.exe'
if (-not (Test-Path -LiteralPath $ResourceCompilerPath -PathType Leaf)) { $ResourceCompilerPath = Find-Executable -Name 'windres.exe' -FullScan }
if (-not $ResourceCompilerPath) {
    Write-Host 'The MinGW-w64 resource compiler windres.exe was not found.'
    Write-Host 'Install the MinGW-w64 binutils package and run build.bat again.'
    exit 1
}
$env:PATH = "$(Split-Path -Parent $ResourceCompilerPath);$env:PATH"
Write-Host "Using compiler: $CompilerPath"
$BuildToolPath = $null
$Generator = $null
$PreferredMake = Join-Path $CompilerDirectory 'mingw32-make.exe'
if (Test-Path -LiteralPath $PreferredMake -PathType Leaf) { $BuildToolPath = $PreferredMake }
if ($BuildToolPath) { $Generator = 'MinGW Makefiles' }
if (-not $BuildToolPath) {
    $PreferredNinja = Join-Path $CompilerDirectory 'ninja.exe'
    if (Test-Path -LiteralPath $PreferredNinja -PathType Leaf) { $BuildToolPath = $PreferredNinja }
    if (-not $BuildToolPath) { $BuildToolPath = Find-Executable -Name 'ninja.exe' }
    if ($BuildToolPath) { $Generator = 'Ninja' }
}
if (-not $BuildToolPath) {
    $BuildToolPath = Find-Executable -Name 'mingw32-make.exe' -FullScan
    if ($BuildToolPath) { $Generator = 'MinGW Makefiles' }
}
if (-not $BuildToolPath) {
    $PreferredMake = Join-Path $CompilerDirectory 'make.exe'
    if (Test-Path -LiteralPath $PreferredMake -PathType Leaf) { $BuildToolPath = $PreferredMake }
    if (-not $BuildToolPath) { $BuildToolPath = Find-Executable -Name 'make.exe' -FullScan }
    if ($BuildToolPath) { $Generator = 'MinGW Makefiles' }
}
if (-not $BuildToolPath) {
    Write-Host 'No compatible make or Ninja executable was found.'
    Write-Host 'Install mingw-w64-ucrt-x86_64-make through the MSYS2 UCRT64 shell.'
    exit 1
}
$CMakePath = Find-Executable -Name 'cmake.exe' -FullScan
if (-not $CMakePath -and (Install-CMake)) { $CMakePath = Find-Executable -Name 'cmake.exe' -FullScan }
if (-not $CMakePath) {
    Write-Host 'CMake was not found. Install CMake and start build.bat again.'
    exit 1
}
Write-Host "Using CMake: $CMakePath"
Write-Host "Using build tool: $BuildToolPath"
if (Test-Path -LiteralPath $BuildDirectory -PathType Container) {
    $CachePath = Join-Path $BuildDirectory 'CMakeCache.txt'
    if (Test-Path -LiteralPath $CachePath -PathType Leaf) {
        $CacheLines = Get-Content -LiteralPath $CachePath -ErrorAction SilentlyContinue
        $CachedCompilerLine = $CacheLines | Where-Object { $_ -match '^CMAKE_CXX_COMPILER(:[^=]+)?=' } | Select-Object -First 1
        $CachedGeneratorLine = $CacheLines | Where-Object { $_ -match '^CMAKE_GENERATOR:INTERNAL=' } | Select-Object -First 1
        $CachedSourceLine = $CacheLines | Where-Object { $_ -match '^CMAKE_HOME_DIRECTORY:INTERNAL=' } | Select-Object -First 1
        $CachedBuildLine = $CacheLines | Where-Object { $_ -match '^CMAKE_CACHEFILE_DIR:INTERNAL=' } | Select-Object -First 1
        $CachedCompiler = if ($CachedCompilerLine -match '^[^=]+=(.*)$') { $Matches[1] } else { '' }
        $CachedGenerator = if ($CachedGeneratorLine -match '^CMAKE_GENERATOR:INTERNAL=(.*)$') { $Matches[1] } else { '' }
        $CachedSource = if ($CachedSourceLine -match '^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$') { $Matches[1] } else { '' }
        $CachedBuild = if ($CachedBuildLine -match '^CMAKE_CACHEFILE_DIR:INTERNAL=(.*)$') { $Matches[1] } else { '' }
        $NormalizePath = { param([string]$Value) if ([string]::IsNullOrWhiteSpace($Value)) { return '' }; return [System.IO.Path]::GetFullPath($Value).TrimEnd([char[]]@('\', '/')) }
        $CompilerChanged = $CachedCompiler -and ((& $NormalizePath $CachedCompiler) -ine (& $NormalizePath $CompilerPath))
        $SourceChanged = $CachedSource -and ((& $NormalizePath $CachedSource) -ine (& $NormalizePath $ProjectRoot))
        $BuildPathChanged = $CachedBuild -and ((& $NormalizePath $CachedBuild) -ine (& $NormalizePath $BuildDirectory))
        if ($CompilerChanged -or $SourceChanged -or $BuildPathChanged -or ($CachedGenerator -and ($CachedGenerator -ne $Generator))) {
            Remove-Item -LiteralPath $BuildDirectory -Recurse -Force
        }
    }
}
$ConfigureArguments = @(
    '-S', $ProjectRoot,
    '-B', $BuildDirectory,
    '-G', $Generator,
    '-DCMAKE_BUILD_TYPE=Release',
    "-DCMAKE_CXX_COMPILER:FILEPATH=$CompilerPath",
    "-DCMAKE_RC_COMPILER:FILEPATH=$ResourceCompilerPath",
    "-DCMAKE_MAKE_PROGRAM:FILEPATH=$BuildToolPath"
)
Write-Host 'Configuring project.'
& $CMakePath @ConfigureArguments
if ($LASTEXITCODE -ne 0) {
    Write-Host 'CMake configuration failed.'
    exit $LASTEXITCODE
}
Write-Host 'Building SauceEngine.'
& $CMakePath --build $BuildDirectory --config Release --parallel
if ($LASTEXITCODE -ne 0) {
    Write-Host 'Build failed.'
    exit $LASTEXITCODE
}
if (-not (Test-Path -LiteralPath $OutputExecutable -PathType Leaf)) {
    Write-Host "Build finished, but the expected executable was not found: $OutputExecutable"
    exit 1
}
Write-Host "Build completed: $OutputExecutable"
if (-not $NoRun) {
    $LaunchExecutable = Join-Path $LaunchRoot 'SauceEngine.exe'
    try {
        Start-Process -FilePath $LaunchExecutable -WorkingDirectory $LaunchRoot -Verb RunAs
    } catch {
        Write-Host 'The executable was built, but Windows did not start it with administrator privileges.'
        Write-Host $_.Exception.Message
        exit 1
    }
}
exit 0
