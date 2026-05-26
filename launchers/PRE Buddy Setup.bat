@echo off
REM PRE Buddy - Windows double-clickable setup launcher.
REM
REM Drop on the Desktop. Double-click to run first-time setup: BLE
REM scan, device pick, autostart prompt.

setlocal

where pre-buddy >nul 2>nul
if %ERRORLEVEL%==0 (
    pre-buddy setup
    goto :done
)

where python >nul 2>nul
if %ERRORLEVEL%==0 (
    python -m pre_buddy.cli setup
    goto :done
)

where py >nul 2>nul
if %ERRORLEVEL%==0 (
    py -3 -m pre_buddy.cli setup
    goto :done
)

echo PRE Buddy not installed.
echo Install with: pip install pre_buddy[tray,transport]
pause
exit /b 1

:done
endlocal
