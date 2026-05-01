@echo off
setlocal EnableExtensions

set "UE_EDITOR=E:\unreal_engine\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe"
set "UE_ROOT=E:\unreal_engine\UE_5.7"
set "PROJECT_ROOT=%~dp0..\..\..\.."
for %%I in ("%PROJECT_ROOT%") do set "PROJECT_ROOT=%%~fI"

set "DECOMPILER_ROOT=%PROJECT_ROOT%\Plugins\autounreal\autounreal\BpyDecompiler"
set "STD_OUT=%DECOMPILER_ROOT%\std"
set "TMP_STD=%PROJECT_ROOT%\tmp\bpydecompiler_std"
set "REFLECTION_JSON=%TMP_STD%\ue_reflection_std.json"
set "REFLECTION_SCRIPT=%DECOMPILER_ROOT%\stdgen\ue_reflection_export.py"
set "UPROJECT=%PROJECT_ROOT%\GameAnimationSample.uproject"
set "REFLECTION_OK="

if not exist "%TMP_STD%" mkdir "%TMP_STD%"

if exist "%UE_EDITOR%" if exist "%UPROJECT%" (
    echo Exporting UE reflection std to %REFLECTION_JSON%
    set "BPYDECOMPILER_REFLECTION_JSON=%REFLECTION_JSON%"
    "%UE_EDITOR%" "%UPROJECT%" -nullrhi -unattended -nop4 -nosplash -run=pythonscript -script="%REFLECTION_SCRIPT%"
    if errorlevel 1 (
        echo Warning: UE reflection export failed; falling back to ExportBpy maps, exported bpy, and UE headers.
    ) else (
        set "REFLECTION_OK=1"
    )
) else (
    echo Warning: UE editor or uproject not found; falling back to ExportBpy maps, exported bpy, and UE headers.
)

if defined REFLECTION_OK (
    py -3 "%DECOMPILER_ROOT%\stdgen\generate_std.py" --project-root "%PROJECT_ROOT%" --ue-root "%UE_ROOT%" --output "%STD_OUT%" --reflection-json "%REFLECTION_JSON%"
) else (
    py -3 "%DECOMPILER_ROOT%\stdgen\generate_std.py" --project-root "%PROJECT_ROOT%" --ue-root "%UE_ROOT%" --output "%STD_OUT%"
)
if errorlevel 1 exit /b %errorlevel%

echo std generated under %STD_OUT%
endlocal
