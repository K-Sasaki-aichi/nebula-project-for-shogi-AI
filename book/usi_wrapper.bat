@echo off
setlocal

REM Ensure working directory is this script's folder (where usi.exe lives)
cd /d "%~dp0"

REM USI protocol must go to stdout; write debug logs to stderr and capture them.
REM Register this .bat as the engine in your GUI to keep stdout clean.

set LOGFILE=usi_stderr.log
echo ===== %date% %time% ===== >> "%LOGFILE%"

REM Run the engine; append stderr to log.
"%~dp0usi.exe" 2>> "%LOGFILE%"

REM Preserve exit code
exit /b %errorlevel%
