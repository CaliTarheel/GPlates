@echo off
REM Configure and build GPlates 2.6.0-dev6-SR0a (Release, Ninja, MSVC 2022, Qt6 from conda).
setlocal

call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1

set ENVROOT=C:\Users\rider\miniforge3\envs\gplates
set PATH=%ENVROOT%\Library\bin;%ENVROOT%\Scripts;%ENVROOT%;%PATH%
set SRC=E:\Derek Plates\.worktrees\sr0a
set BLD=%SRC%\build

"%ENVROOT%\Library\bin\cmake.exe" -S "%SRC%" -B "%BLD%" -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_PREFIX_PATH="%ENVROOT%\Library" ^
  -DGPLATES_INSTALL_STANDALONE=OFF
if errorlevel 1 exit /b 1

"%ENVROOT%\Library\bin\cmake.exe" --build "%BLD%" --target gplates
exit /b %errorlevel%
