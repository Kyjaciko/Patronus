@echo off

set "ROOT=%~dp0"
set "BUILD_DIR=%ROOT%build"
set "TARGET=D3D12HelloTriangle"
set "EXE=%BUILD_DIR%\Debug\%TARGET%.exe"

echo Building %TARGET%...
cmake --build "%BUILD_DIR%" --config Debug --target "%TARGET%"

if errorlevel 1 (
  echo.
  echo Build failed.
  exit /b 1
)

echo.
echo Starting %TARGET%...
"%EXE%"