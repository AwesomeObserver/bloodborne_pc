@echo off
setlocal
cd /d "%~dp0"
set "BB_RTX_PATH_TRACE=1"
set "BB_RTX_MODE=1"
"Bloodborne.exe" --play
