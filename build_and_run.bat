@echo off
setlocal

rem Usage: build_and_run.bat [Debug|Release|RelWithDebInfo]   (default: Debug)
rem Release is the configuration for benchmark numbers: shaders are built
rem with -O3 instead of -Od and the D3D12 debug layer is off.

set "ROOT=%~dp0"
set "BUILD_DIR=%ROOT%build"
set "TARGET=D3D12HelloTriangle"
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Debug"
if /i not "%CONFIG%"=="Debug" if /i not "%CONFIG%"=="Release" if /i not "%CONFIG%"=="RelWithDebInfo" goto :bad_config
set "EXE=%BUILD_DIR%\%CONFIG%\%TARGET%.exe"
if exist "%BUILD_DIR%\CMakeCache.txt" goto :build

echo Configuring %TARGET%...
pushd "%ROOT%."
cmake --preset windows
set "RC=%ERRORLEVEL%"
popd
if not "%RC%"=="0" goto :configure_failed

:build
echo Building %TARGET% (%CONFIG%)...
cmake --build "%BUILD_DIR%" --config %CONFIG% --target "%TARGET%"
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" goto :build_failed
if not exist "%EXE%" goto :missing_exe

echo.
echo Starting %TARGET% (%CONFIG%)...
"%EXE%"
set "RC=%ERRORLEVEL%"
echo.
echo Program exited with code: %RC%
exit /b %RC%

:bad_config
echo Unknown configuration "%CONFIG%". Use Debug, Release or RelWithDebInfo.
pause
exit /b 1

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
