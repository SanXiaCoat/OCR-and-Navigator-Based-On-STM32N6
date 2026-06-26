@echo off
REM 生成带起始地址的 appli.hex，供 CubeProgrammer 烧录 XSPI2 NOR @ 0x70100400
setlocal EnableDelayedExpansion

set "PROJ_DIR=%~dp0"
set "PROJECT_OUT="

if exist "%PROJ_DIR%Release\AI_Vision_Appli.bin" (
  set "PROJECT_OUT=%PROJ_DIR%Release\AI_Vision_Appli.bin"
) else if exist "%PROJ_DIR%Debug\AI_Vision_Appli.bin" (
  set "PROJECT_OUT=%PROJ_DIR%Debug\AI_Vision_Appli.bin"
)

if not defined PROJECT_OUT (
  echo [postbuild] AI_Vision_Appli.bin not found, skip appli.hex
  exit /b 0
)

if not exist "%PROJ_DIR%..\..\Binary" mkdir "%PROJ_DIR%..\..\Binary"

arm-none-eabi-objcopy -I binary "%PROJECT_OUT%" --change-addresses 0x70100400 -O ihex "%PROJ_DIR%..\..\Binary\appli.hex"
if errorlevel 1 (
  echo [postbuild] objcopy failed
  exit /b 1
)

echo [postbuild] Generated %PROJ_DIR%..\..\Binary\appli.hex from %PROJECT_OUT%
exit /b 0
