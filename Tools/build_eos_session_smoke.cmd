@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "PROBEOUT=E:\UnrealProjects\CitixChase\Saved\EOSSessionProbe"
if not exist "%PROBEOUT%" mkdir "%PROBEOUT%"
cl /nologo /std:c++17 /EHsc /MT /W4 /O2 /I"E:\UE_5.8\Engine\Source\ThirdParty\EOSSDK\SDK\Include" /Fo"%PROBEOUT%\session.obj" /Fe"%PROBEOUT%\CitixEOSSessionSmoke.exe" "%~dp0eos_session_smoke_main.cpp" /link /LIBPATH:"E:\UE_5.8\Engine\Source\ThirdParty\EOSSDK\SDK\Lib" EOSSDK-Win64-Shipping.lib
exit /b %errorlevel%
