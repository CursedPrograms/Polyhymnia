@echo off
setlocal
cd /d "%~dp0"
set "SRC=dist\Polyhymnia"
if exist "Polyhymnia.exe" set "SRC=."
if not exist "%SRC%\Polyhymnia.exe" ( echo Build it first: build.bat & exit /b 1 )
tasklist /fi "imagename eq Polyhymnia.exe" | find /i "Polyhymnia.exe" >nul
if not errorlevel 1 ( echo Polyhymnia is open: close it and run this again. & exit /b 1 )
set "INST=%LOCALAPPDATA%\Programs\Polyhymnia"
if not exist "%INST%" mkdir "%INST%"
xcopy /e /i /q /y "%SRC%\*" "%INST%\" >nul || ( echo Couldn't copy to %INST% & exit /b 1 )
powershell -NoProfile -Command "$s=New-Object -ComObject WScript.Shell; foreach($d in @([Environment]::GetFolderPath('Programs'),[Environment]::GetFolderPath('Desktop'))){ $l=$s.CreateShortcut((Join-Path $d 'Polyhymnia.lnk')); $l.TargetPath='%INST%\Polyhymnia.exe'; $l.WorkingDirectory='%INST%'; $l.IconLocation='%INST%\Polyhymnia.exe,0'; $l.Description='Polyhymnia: arrange and mix music'; $l.Save() }"
echo Installed: %INST%
exit /b 0