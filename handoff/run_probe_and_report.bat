@echo off
rem KieeKey BS-24 - one-click field evidence collector (Phase 0/1)
rem Runs collect_report.ps1: facts + probe + zip. See the guide in this folder.
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0collect_report.ps1"
