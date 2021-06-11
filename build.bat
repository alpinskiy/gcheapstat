@echo off

if "%1"=="/?" (
  echo Usage:
  echo   build.bat Configuration Platform
  echo.
  echo   Configuration 'Debug' or 'Release'
  echo   Platform      'x86' or 'x64'
  exit /B 0
)

set Configuration=%1%
if "%Configuration%"=="" (set Configuration=Debug)

set Platform=%2%
if "%Platform%"=="" (
  if "%PROCESSOR_ARCHITECTURE%"=="x86" (set Platform=x86) else (
	  if /I "%PROCESSOR_ARCHITECTURE%"=="amd64" (set Platform=x64)
  )
)

set CMakeArchitecture=
if /I "%Platform%"=="x86" (set CMakeArchitecture=Win32) else (
  if /I "%Platform%"=="x64" (set CMakeArchitecture=x64)
)

set CMakeSourceDir=%~dp0
set CMakeBuildDir=%~dp0build\windows-%Platform%
if not defined DevEnvDir (
  call "%PROGRAMFILES(x86)%\Microsoft Visual Studio\2019\Community\Common7\Tools\VsDevCmd.bat"
  if errorlevel 1 (
    call "%PROGRAMFILES(x86)%\Microsoft Visual Studio\2019\Professional\Common7\Tools\VsDevCmd.bat"
    if errorlevel 1 (
      call "%PROGRAMFILES(x86)%\Microsoft Visual Studio\2019\Enterprise\Common7\Tools\VsDevCmd.bat"
    )
  )
)

cmake -S %CMakeSourceDir% -B %CMakeBuildDir% -G "Visual Studio 16 2019" -DCMAKE_BUILD_TYPE=%Configuration% -A %CMakeArchitecture%
if errorlevel 1 (exit /B 1)
cmake --build "%CMakeBuildDir%" --config %Configuration%
