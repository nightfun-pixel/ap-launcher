@echo off
rem BLUEWARD build: vswhere -> vcvars64 -> CMake/Ninja Release -> build -> tests.
rem --clean-first is deliberate: localized MSVC /showIncludes output can lose header dependencies
rem and leave old code in a "successful" build.
setlocal
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
set "VS="
for /f "usebackq delims=" %%i in (`vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS=%%i"
if not defined VS (
  echo Visual Studio C++ tools not found.
  exit /b 1
)
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

cmake -S "%~dp0." -B "%~dp0build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=cl || exit /b 1
cmake --build "%~dp0build" --clean-first || exit /b 1
"%~dp0build\blueward_tests.exe" || exit /b 1

echo.
echo Built: %~dp0dist\BLUEWARD.exe
