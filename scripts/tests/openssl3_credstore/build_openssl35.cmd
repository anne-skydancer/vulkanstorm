@echo off
rem SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later
rem
rem Build OpenSSL 3.5.x the way doc/openssl3-impact.md proposes to ship it:
rem static (no-shared), legacy provider compiled into libcrypto (no-module),
rem zlib from the viewer's zlib-ng package, as LL's 3p-openssl does.
rem
rem Usage: build_openssl35.cmd <verified openssl-3.5.x.tar.gz> <work dir> <viewer packages dir>
rem   The tarball must already be verified (SHA-256 and the release signature).
rem   Nothing is written inside the repository. Needs Strawberry Perl
rem   (C:\Strawberry\perl\bin), NASM on PATH or in %LOCALAPPDATA%\bin\NASM,
rem   and Visual Studio's x64 build tools.
setlocal
if "%~3"=="" (echo usage: %~nx0 tarball workdir packagesdir & exit /b 2)
set TARBALL=%~f1
set WORK=%~f2
set PKG=%~f3
if not exist "%TARBALL%" (echo missing %TARBALL% & exit /b 2)
if not exist "%PKG%\include\zlib-ng\zlib.h" (echo missing zlib-ng in %PKG% & exit /b 2)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`call "%VSWHERE%" -latest -property installationPath`) do set "VSDIR=%%i"
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set PATH=C:\Strawberry\perl\bin;%LOCALAPPDATA%\bin\NASM;%PATH%

rem LL's 3p-openssl replaces CFLAGS with the viewer's release switches (/MD,
rem the dynamic CRT the zlib-ng package and the viewer use); OpenSSL's own
rem default is /MT, which does not link against that zlib.
set CFLAGS=/nologo /O2 /MD /W3 /wd4090 /DNDEBUG
set CXXFLAGS=%CFLAGS%

if exist "%WORK%\src" rmdir /s /q "%WORK%\src"
mkdir "%WORK%\src"
tar -xzf "%TARBALL%" -C "%WORK%\src" || exit /b 1
for /d %%d in ("%WORK%\src\openssl-3.5.*") do set SRC=%%d
cd /d "%SRC%" || exit /b 1

perl Configure VC-WIN64A zlib no-zlib-dynamic threads no-shared no-module ^
    -DUNICODE -D_UNICODE -FS ^
    --prefix="%WORK%\install" --openssldir="%WORK%\install\ssl" ^
    --with-zlib-include="%PKG%\include\zlib-ng" ^
    --with-zlib-lib="%PKG%\lib\release\zlib.lib" || exit /b 1
nmake /nologo || exit /b 1
nmake /nologo install_sw || exit /b 1
"%WORK%\install\bin\openssl.exe" version -a
"%WORK%\install\bin\openssl.exe" list -providers -provider legacy -provider default
endlocal
