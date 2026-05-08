@echo off
rem atari-hd launcher shim (Windows).
rem
rem Lives at %LOCALAPPDATA%\atari-hd\atari-hd.cmd; the installer also
rem drops a copy in %LOCALAPPDATA%\Microsoft\WindowsApps\ (which is on
rem every Windows 10+ user's PATH by default). Resolves the package
rem directory from %~dp0 and runs atari_hd.py with the user's args.
rem
rem python (or python3) must be on PATH; we try both since the
rem Microsoft Store stub launches as `python` and the python.org
rem installer adds `python` + `python3` aliases.

setlocal
set "PKG_DIR=%~dp0"
if "%PKG_DIR:~-1%"=="\" set "PKG_DIR=%PKG_DIR:~0,-1%"

where python3 >nul 2>nul
if %ERRORLEVEL%==0 (
    python3 "%PKG_DIR%\atari_hd.py" %*
    exit /b %ERRORLEVEL%
)

where python >nul 2>nul
if %ERRORLEVEL%==0 (
    python "%PKG_DIR%\atari_hd.py" %*
    exit /b %ERRORLEVEL%
)

echo atari-hd: python not found on PATH. 1>&2
echo   Install Python 3.10+ from https://www.python.org/downloads/ 1>&2
exit /b 127
