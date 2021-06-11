@echo off

echo #define BUILD %APPVEYOR_BUILD_NUMBER% > build_number.txt
call build.bat Release x86
call build.bat Release x64
python %~dp0artifacts.py
