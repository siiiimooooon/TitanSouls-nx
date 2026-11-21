@echo off
rem Installs WSL2 with Ubuntu (once). It asks for administrator rights, and the PC has to be restarted afterwards.
rem What it does: "wsl --install -d Ubuntu --no-launch" (a download from Microsoft, ~1 GB).
rem After the restart, Ubuntu and Docker still need setting up for the build (see docs/tools.md).

net session >nul 2>&1
if %errorlevel% neq 0 (
    echo Asking for administrator rights...
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

echo.
echo === Installing WSL2 + Ubuntu (this can take several minutes) ===
echo.
wsl --install -d Ubuntu --no-launch
if %errorlevel% neq 0 (
    echo.
    echo ERROR: the installation failed (code %errorlevel%). Take a picture of this window.
    pause
    exit /b 1
)

echo.
echo === Done. RESTART the PC. ===
pause
