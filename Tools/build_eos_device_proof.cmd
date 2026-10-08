@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "PROOFOUT=E:\UnrealProjects\CitixChase\EOSDeviceProofKit"
if not exist "%PROOFOUT%" mkdir "%PROOFOUT%"
cl /nologo /std:c++17 /EHsc /MT /W4 /O2 /I"E:\UE_5.8\Engine\Source\ThirdParty\EOSSDK\SDK\Include" /Fo"%PROOFOUT%\eos_device_proof_main.obj" /Fe"%PROOFOUT%\CitixEOSDeviceProof.exe" "%~dp0eos_device_proof_main.cpp" /link /LIBPATH:"E:\UE_5.8\Engine\Source\ThirdParty\EOSSDK\SDK\Lib" EOSSDK-Win64-Shipping.lib Advapi32.lib Bcrypt.lib
exit /b %errorlevel%
