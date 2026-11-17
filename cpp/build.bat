@echo off
setlocal
cd /d "%~dp0"
rem Builds Polyhymnia (Release) into build\Release and dist\Polyhymnia-win64.zip.
if not exist "external\SDL2-2.30.9\cmake" (
    echo Getting SDL2's development package, once...
    if not exist external mkdir external
    curl -sL -o external\SDL2-devel.zip https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-devel-2.30.9-VC.zip || goto :error
    tar -xf external\SDL2-devel.zip -C external || goto :error
    del external\SDL2-devel.zip
)
cmake -B build -G "Visual Studio 17 2022" -A x64 || goto :error
cmake --build build --config Release || goto :error
if exist dist\Polyhymnia rmdir /s /q dist\Polyhymnia
mkdir dist\Polyhymnia
xcopy /e /i /q /y build\Release\* dist\Polyhymnia\ >nul
del /q dist\Polyhymnia\*.lib 2>nul
set "CRT="
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -find VC\Redist\MSVC\*\x64\Microsoft.VC143.CRT`) do set "CRT=%%i"
if not defined CRT echo Visual C++ runtime folder not found.& goto :error
for %%f in (vcruntime140.dll vcruntime140_1.dll msvcp140.dll msvcp140_1.dll) do copy /y "%CRT%\%%f" dist\Polyhymnia\ >nul || goto :error
if exist dist\Polyhymnia-win64.zip del dist\Polyhymnia-win64.zip
"%SystemRoot%\System32\tar.exe" -a -c -f dist\Polyhymnia-win64.zip -C dist\Polyhymnia * || goto :error
echo Built: build\Release\Polyhymnia.exe and dist\Polyhymnia-win64.zip
exit /b 0
:error
echo Build failed, see above.
exit /b 1