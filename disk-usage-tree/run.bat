@echo off
REM Disk Usage Tree launcher. Run as administrator to read more folders.
chcp 65001 > nul
cd /d "%~dp0"
where py > nul 2>&1
if %errorlevel%==0 (
    py -3 server.py %*
) else (
    python server.py %*
)
pause
