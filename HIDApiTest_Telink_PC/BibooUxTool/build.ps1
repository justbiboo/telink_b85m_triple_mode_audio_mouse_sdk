$ErrorActionPreference = 'Continue'
$here = $PSScriptRoot
$sln = Join-Path $here "BibooUxTool.sln"
$msbuild = "E:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"
$log = Join-Path $here "build_log.txt"

"==== RESTORE ====" | Out-File $log -Encoding UTF8
& $msbuild $sln /t:Restore /p:Configuration=Debug /p:Platform=x64 /m /v:m /nologo >> $log
"restore exit = $LASTEXITCODE" | Out-File $log -Append -Encoding UTF8

"==== BUILD ====" | Out-File $log -Append -Encoding UTF8
& $msbuild $sln /p:Configuration=Debug /p:Platform=x64 /m /v:m /nologo >> $log
"build exit = $LASTEXITCODE" | Out-File $log -Append -Encoding UTF8
