@echo off
rem SPDX-License-Identifier: GPL-3.0-or-later
rem Build the libdeflate bench (MSVC, x64, like the viewer: /O2 /arch:AVX2 /MD).
rem   build_bench.cmd <viewer build dir> <libdeflate source dir> <out dir>
rem The viewer build dir supplies the Release llcommon/llmath/llmeshoptimizer
rem libraries (LTO) and the autobuild packages (zlib-ng 2.2.5, boost, apr...).
rem The libraries are copied into <out dir>\vlibs first, so a later viewer
rem build cannot change them under the bench.
setlocal
set VB=%~f1
set LD=%~f2
set OUT=%~f3
set HERE=%~dp0
set REPO=%HERE%..\..\..
if "%OUT%"=="" (echo usage: build_bench.cmd viewer-build libdeflate-src out & exit /b 2)
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist "%OUT%\vlibs" mkdir "%OUT%\vlibs"
if not exist "%OUT%\obj" mkdir "%OUT%\obj"
if not exist "%OUT%\obj_asan" mkdir "%OUT%\obj_asan"
if not exist "%OUT%\obj_bench" mkdir "%OUT%\obj_bench"
copy /y "%VB%\llcommon\Release\llcommon.lib" "%OUT%\vlibs\" >nul || exit /b 1
copy /y "%VB%\llmath\Release\llmath.lib" "%OUT%\vlibs\" >nul || exit /b 1
copy /y "%VB%\llmeshoptimizer\Release\llmeshoptimizer.lib" "%OUT%\vlibs\" >nul || exit /b 1
copy /y "%VB%\packages\lib\release\zlib.lib" "%OUT%\vlibs\" >nul || exit /b 1
set PK=%VB%\packages
set ZINC=%PK%\include\zlib-ng
set LDFLAGS_C=/nologo /c /O2 /arch:AVX2 /MD /W3 /DNDEBUG /I"%LD%"

rem libdeflate 1.26: every library source (the arm/riscv files compile to nothing on x64)
pushd "%OUT%\obj"
del /q *.obj 2>nul
cl %LDFLAGS_C% "%LD%\lib\*.c" "%LD%\lib\x86\cpu_features.c" || (popd & exit /b 1)
lib /nologo /OUT:"%OUT%\libdeflate.lib" *.obj || (popd & exit /b 1)
popd
pushd "%OUT%\obj_asan"
del /q *.obj 2>nul
cl %LDFLAGS_C% /Zi /fsanitize=address "%LD%\lib\*.c" "%LD%\lib\x86\cpu_features.c" || (popd & exit /b 1)
lib /nologo /OUT:"%OUT%\libdeflate_asan.lib" *.obj || (popd & exit /b 1)
popd

set CXXCOMMON=/nologo /O2 /Oi /Ot /arch:AVX2 /MD /EHsc /std:c++20 /GR /fp:precise /W3 /DNDEBUG /DWIN32 /D_WINDOWS /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I"%ZINC%" /I"%LD%"

rem plain build: zlib-ng + libdeflate only (speed, latency, synthetic, gzfile, fuzz)
cl %CXXCOMMON% /DBENCH_NO_VIEWER "%HERE%bench_libdeflate.cpp" /Fo"%OUT%\obj_bench\bench_plain.obj" /Fe"%OUT%\bench_plain.exe" /link "%OUT%\vlibs\zlib.lib" "%OUT%\libdeflate.lib" || exit /b 1
rem plain build with AddressSanitizer for the fuzz pass
cl %CXXCOMMON% /Zi /fsanitize=address /DBENCH_NO_VIEWER "%HERE%bench_libdeflate.cpp" /Fo"%OUT%\obj_bench\bench_plain_asan.obj" /Fd"%OUT%\obj_bench\bench_plain_asan.pdb" /Fe"%OUT%\bench_plain_asan.exe" /link "%OUT%\vlibs\zlib.lib" "%OUT%\libdeflate_asan.lib" || exit /b 1

rem viewer-linked build: the viewer's Release compile definitions and LTO
set VDEF=/DLL_RELEASE=1 /D_SECURE_STL=0 /D_HAS_ITERATOR_DEBUGGING=0 /DLL_WINDOWS=1 /DUNICODE /D_UNICODE /DWINVER=0x0601 /D_WIN32_WINNT=0x0601 /DLL_OS_DRAGDROP_ENABLED=1 /DLIB_NDOF=1 /DUSE_AVX2_OPTIMIZATION /DADDRESS_SIZE=64 /DBOOST_BIND_GLOBAL_PLACEHOLDERS /DGLM_FORCE_DEFAULT_ALIGNED_GENTYPES=1 /DGLM_ENABLE_EXPERIMENTAL=1 /DSSE2NEON_SUPPRESS_WARNINGS=1 /DUSE_LTO /D_CRT_SECURE_NO_WARNINGS /D_CRT_NONSTDC_NO_DEPRECATE /D_CRT_OBSOLETE_NO_WARNINGS /D_WINSOCK_DEPRECATED_NO_WARNINGS /DAPR_DECLARE_STATIC=1 /DAPU_DECLARE_STATIC=1 /DAPI_DECLARE_STATIC=1 /DXML_STATIC=1
set VINC=/I"%REPO%\indra\llcommon" /I"%REPO%\indra\llmeshoptimizer" /I"%REPO%\indra\llmath" /external:W0 /external:I"%PK%\include\apr-1" /external:I"%PK%\include" /external:I"%PK%\include\meshoptimizer"
set BOOSTLIBS=libboost_context.lib libboost_fiber.lib libboost_filesystem.lib libboost_program_options.lib libboost_thread.lib libboost_url.lib libboost_json.lib libboost_atomic.lib libboost_chrono.lib libboost_date_time.lib libboost_iostreams.lib
cl %CXXCOMMON% /GL /permissive- %VDEF% %VINC% "%HERE%bench_libdeflate.cpp" /Fo"%OUT%\obj_bench\bench_viewer.obj" /Fe"%OUT%\bench_viewer.exe" /link /LTCG /LIBPATH:"%PK%\lib\release" "%OUT%\vlibs\llmath.lib" "%OUT%\vlibs\llmeshoptimizer.lib" "%OUT%\vlibs\llcommon.lib" meshoptimizer.lib "%OUT%\vlibs\zlib.lib" "%OUT%\libdeflate.lib" apr-1.lib aprutil-1.lib libexpat.lib %BOOSTLIBS% ws2_32.lib advapi32.lib user32.lib shell32.lib ole32.lib dbghelp.lib iphlpapi.lib rpcrt4.lib psapi.lib netapi32.lib userenv.lib bcrypt.lib wldap32.lib mswsock.lib || exit /b 1
echo built: %OUT%\bench_viewer.exe %OUT%\bench_plain.exe %OUT%\bench_plain_asan.exe
