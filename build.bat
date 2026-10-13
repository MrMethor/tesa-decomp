@echo off
rem Build with the Visual Studio C compiler and its bundled CMake.
rem Output goes to build\ (bsatool.exe, ...).
setlocal
set VS=C:\Program Files\Microsoft Visual Studio\18\Community
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set CMAKE=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
"%CMAKE%" -S "%~dp0." -B "%~dp0build" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release >nul || exit /b 1
"%CMAKE%" --build "%~dp0build"
