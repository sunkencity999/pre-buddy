@echo off
REM PRE Buddy - Windows double-clickable tray launcher.
REM
REM Drop on the Desktop or pin to the Start menu. Double-click to bring
REM up the notification-area icon.

setlocal

where pre-buddy >nul 2>nul
if %ERRORLEVEL%==0 (
    start "" pre-buddy tray
    goto :done
)

where pythonw >nul 2>nul
if %ERRORLEVEL%==0 (
    REM pythonw avoids a flashing console window for the GUI app.
    start "" pythonw -m pre_buddy.cli tray
    goto :done
)

where python >nul 2>nul
if %ERRORLEVEL%==0 (
    start "" python -m pre_buddy.cli tray
    goto :done
)

echo PRE Buddy not installed.
echo Install with: pip install pre_buddy[tray,transport]
pause
exit /b 1

:done
endlocal
