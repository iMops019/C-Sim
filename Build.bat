@echo off
echo Building C-Sim...
cmake --build "%~dp0build" --config Debug
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo BUILD FAILED - see errors above.
    pause
    exit /b 1
)
echo.
echo Build succeeded.
pause
