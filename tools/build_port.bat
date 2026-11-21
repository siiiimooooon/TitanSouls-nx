@echo off
rem Builds the Titan Souls port in WSL and leaves the NRO in out\ (next to tools\)
pushd "%~dp0"
wsl -d Ubuntu-24.04 -u nx -- bash build_port.sh %*
popd
pause
