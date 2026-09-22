@echo off
setlocal

set "ROOT=%~dp0"
set "BUILD_DIR=%ROOT%build"
set "TARGET=D3D12HelloTriangle"
set "EXE=%BUILD_DIR%\Debug\%TARGET%.exe"
if exist "%BUILD_DIR%\CMakeCache.txt" goto :build

echo Configuring %TARGET%...
pushd "%ROOT%."
cmake --preset windows
set "RC=%ERRORLEVEL%"
popd
if not "%RC%"=="0" goto :configure_failed

:build
echo Building %TARGET%...
cmake --build "%BUILD_DIR%" --config Debug --target "%TARGET%"
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" goto :build_failed
if not exist "%EXE%" goto :missing_exe

echo.
echo Starting %TARGET%...
"%EXE%"
set "RC=%ERRORLEVEL%"
echo.
echo Program exited with code: %RC%
exit /b %RC%

:configure_failed
echo.
echo Configure failed ^(exit code %RC%^). Not building.
pause
exit /b 1

:build_failed
echo.
echo Build failed ^(exit code %RC%^). Not starting %TARGET%.
pause
exit /b 1

:missing_exe
echo.
echo Build succeeded but "%EXE%" is missing. Not starting %TARGET%.
pause
exit /b 1