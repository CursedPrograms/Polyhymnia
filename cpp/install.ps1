$ErrorActionPreference = 'Stop'
& cmd /c "`"$PSScriptRoot\install.bat`""
exit $LASTEXITCODE