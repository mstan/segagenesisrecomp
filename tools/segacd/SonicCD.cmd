@echo off
setlocal
title Sonic CD
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0SonicCD.ps1" %*
exit /b %errorlevel%
