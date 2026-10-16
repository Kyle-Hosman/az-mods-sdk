@echo off
rem SPDX-License-Identifier: GPL-3.0-only
rem azmod-pack for Windows: runs tools\azmod-pack with Python 3.
rem Usage: tools\azmod-pack.cmd build template
rem An activated virtual environment (VIRTUAL_ENV) wins: its python. Otherwise the py launcher,
rem without a version switch so it too honors an active environment, and plain python when
rem the launcher is not installed. Labels, not a parenthesized block: %ERRORLEVEL% inside a
rem block is read before the command in it runs.
if defined VIRTUAL_ENV goto python
where py >nul 2>nul
if errorlevel 1 goto python
py "%~dp0azmod-pack" %*
exit /b %ERRORLEVEL%
:python
python "%~dp0azmod-pack" %*
exit /b %ERRORLEVEL%
