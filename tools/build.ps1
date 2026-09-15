param([string]$Configuration = 'Debug', [string]$OutputDirectory = '')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$installation = & $vswhere -latest -requires Microsoft.Component.MSBuild -property installationPath
$info = [Diagnostics.ProcessStartInfo]::new()
$info.FileName = Join-Path $installation 'MSBuild/Current/Bin/MSBuild.exe'
$info.Arguments = "CG2_Setup.sln /m:1 /nr:false /p:Configuration=$Configuration /p:Platform=x64 /verbosity:normal /nologo /fl /flp:logfile=build-$Configuration.log;verbosity=normal"
if ($OutputDirectory) {
    $outputPath = [IO.Path]::GetFullPath($OutputDirectory)
    if (!$outputPath.StartsWith((Split-Path $projectRoot) + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Output directory must be within the workspace.'
    }
    $info.Arguments += ' /p:OutDir="' + $outputPath.TrimEnd('\') + '\\"'
}
$info.WorkingDirectory = $projectRoot
$info.UseShellExecute = $false
$info.CreateNoWindow = $true
$pathValue = $env:PATH
[void]$info.Environment.Remove('PATH')
[void]$info.Environment.Remove('Path')
$info.Environment['Path'] = $pathValue
$process = [Diagnostics.Process]::Start($info)
$process.WaitForExit()
exit $process.ExitCode
