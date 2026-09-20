@echo off
echo Building C-Sim (Release: optimized, much faster audio)...
cmake --build "%~dp0build" --config Release --target CSim
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo BUILD FAILED - see errors above.
    pause
    exit /b 1
)
echo.
echo Build succeeded. Run it with RunRelease.bat
pause
