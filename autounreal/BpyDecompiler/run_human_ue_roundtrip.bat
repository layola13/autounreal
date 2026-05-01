@echo off
setlocal EnableExtensions

set "UE_EDITOR=E:\unreal_engine\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe"
set "PROJECT_ROOT=%~dp0..\..\..\.."
for %%I in ("%PROJECT_ROOT%") do set "PROJECT_ROOT=%%~fI"

set "UPROJECT=%PROJECT_ROOT%\GameAnimationSample.uproject"
set "DECOMPILER_ROOT=%PROJECT_ROOT%\Plugins\autounreal\autounreal\BpyDecompiler"
set "RUNNER=%DECOMPILER_ROOT%\tools\run_human_ue_roundtrip_unreal.py"

if "%~1"=="" (
    echo Usage: %~nx0 ^<source-bpy-or-human-dir^> ^<tmp-work-dir^> ^<label^> ^<target-asset-path^> [--source-is-human] [--no-compile-asset]
    echo Example: %~nx0 "%PROJECT_ROOT%\ExportedBlueprints\bpy\SandboxCharacter_Mover" "%PROJECT_ROOT%\tmp\bpydecompiler_ue_roundtrip_cbp" CBPUE /Game/tmp/BpyDecompiler/CBPUE
    exit /b 2
)
if "%~2"=="" exit /b 2
if "%~3"=="" exit /b 2
if "%~4"=="" exit /b 2

if not exist "%UE_EDITOR%" (
    echo UnrealEditor.exe not found: %UE_EDITOR%
    exit /b 3
)
if not exist "%UPROJECT%" (
    echo UProject not found: %UPROJECT%
    exit /b 3
)

set "SOURCE=%~f1"
set "WORK=%~f2"
set "LABEL=%~3"
set "TARGET=%~4"
shift
shift
shift
shift

"%UE_EDITOR%" "%UPROJECT%" -nullrhi -unattended -nop4 -nosplash -run=pythonscript -script="%RUNNER%" -- --source "%SOURCE%" --work "%WORK%" --label "%LABEL%" --target-path "%TARGET%" %*
exit /b %errorlevel%
