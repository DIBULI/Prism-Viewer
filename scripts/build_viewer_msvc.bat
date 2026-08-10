@echo off
setlocal

set "ROOT=%~dp0.."
set "BUILD_DIR=%ROOT%\build-msvc"
set "QT_PREFIX=%~1"

rem Keep one reusable build directory, but discard CMake's absolute source-path
rem cache so the Viewer continues to build after the repository is moved.
if exist "%BUILD_DIR%\CMakeCache.txt" del /f /q "%BUILD_DIR%\CMakeCache.txt"
if exist "%BUILD_DIR%\CMakeFiles" rmdir /s /q "%BUILD_DIR%\CMakeFiles"

where cl.exe >nul 2>nul
if errorlevel 1 (
  if exist "%ProgramFiles%\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" (
    call "%ProgramFiles%\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
  ) else if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" (
    call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
  ) else (
    echo cl.exe was not found. Run this from a Visual Studio Developer Command Prompt.
    exit /b 1
  )
)

if "%QT_PREFIX%"=="" (
  cmake -S "%ROOT%" -B "%BUILD_DIR%" -A x64 -DCMAKE_BUILD_TYPE=Release
) else (
  cmake -S "%ROOT%" -B "%BUILD_DIR%" -A x64 -DCMAKE_PREFIX_PATH="%QT_PREFIX%" -DCMAKE_BUILD_TYPE=Release
)
if errorlevel 1 exit /b 1

cmake --build "%BUILD_DIR%" --config Release
if errorlevel 1 exit /b 1

ctest --test-dir "%BUILD_DIR%" -C Release -R windows-runtime-loader --output-on-failure
if errorlevel 1 exit /b 1

set "WINDEPLOYQT="
if not "%QT_PREFIX%"=="" (
  if exist "%QT_PREFIX%\bin\windeployqt.exe" (
    set "WINDEPLOYQT=%QT_PREFIX%\bin\windeployqt.exe"
  )
)

if not defined WINDEPLOYQT (
  for /f "delims=" %%I in ('where windeployqt.exe 2^>nul') do (
    if not defined WINDEPLOYQT set "WINDEPLOYQT=%%I"
  )
)

if not defined WINDEPLOYQT (
  for /f "delims=" %%I in ('dir /b /s "C:\Qt\windeployqt.exe" 2^>nul') do (
    if not defined WINDEPLOYQT set "WINDEPLOYQT=%%I"
  )
)

if not defined WINDEPLOYQT (
  echo windeployqt.exe was not found. Pass the Qt prefix as the first argument.
  exit /b 1
)

set "VIEWER_EXE=%BUILD_DIR%\Release\prism-viewer.exe"
if not exist "%VIEWER_EXE%" set "VIEWER_EXE=%BUILD_DIR%\prism-viewer.exe"
"%WINDEPLOYQT%" --release --no-translations --compiler-runtime "%VIEWER_EXE%"
if errorlevel 1 exit /b 1

echo Viewer package is ready in "%BUILD_DIR%".
