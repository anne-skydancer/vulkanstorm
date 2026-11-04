@echo off
rem SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later
rem
rem Build the credential-store harness twice: against the OpenSSL 1.1.1
rem package the viewer uses (a build tree's packages directory) and against
rem an OpenSSL 3.5 install from build_openssl35.cmd.
rem
rem Usage: build.cmd <viewer packages dir> <openssl 3.5 install dir> <out dir>
rem Produces <out>\credstore111.exe and <out>\credstore35.exe.
setlocal
if "%~3"=="" (echo usage: %~nx0 packagesdir openssl35dir outdir & exit /b 2)
set PKG=%~f1
set O3=%~f2
set OUT=%~f3
set SRC=%~dp0credstore.cpp
if not exist "%PKG%\lib\release\libcrypto.lib" (echo missing %PKG%\lib\release\libcrypto.lib & exit /b 2)
if not exist "%O3%\lib\libcrypto.lib" (echo missing %O3%\lib\libcrypto.lib & exit /b 2)
if not exist "%OUT%" mkdir "%OUT%"

rem No parenthesised block here: "%ProgramFiles(x86)%" would end it.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
where cl >nul 2>nul && goto :havecl
for /f "usebackq delims=" %%i in (`call "%VSWHERE%" -latest -property installationPath`) do set "VSDIR=%%i"
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
:havecl
set SYSLIBS=ws2_32.lib crypt32.lib advapi32.lib user32.lib
set CLFLAGS=/nologo /std:c++17 /EHsc /O2 /MD /W4 /WX

cl %CLFLAGS% /Fo"%OUT%\credstore111.obj" /Fe"%OUT%\credstore111.exe" /I"%PKG%\include" "%SRC%" ^
   /link /LIBPATH:"%PKG%\lib\release" libcrypto.lib zlib.lib %SYSLIBS% || exit /b 1
cl %CLFLAGS% /DVKS_OPENSSL3 /DOPENSSL_API_COMPAT=30000 /DOPENSSL_NO_DEPRECATED /Fo"%OUT%\credstore35.obj" /Fe"%OUT%\credstore35.exe" /I"%O3%\include" "%SRC%" ^
   /link /LIBPATH:"%O3%\lib" /LIBPATH:"%PKG%\lib\release" libcrypto.lib zlib.lib %SYSLIBS% || exit /b 1
endlocal
