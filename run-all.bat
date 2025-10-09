@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem Get ESC for ANSI color (Win10+)
for /f "delims=" %%A in ('echo prompt $E^| cmd') do set "ESC=%%A"
set "RED=%ESC%[31m"
set "RESET=%ESC%[0m"

rem Walk all .asm files under .\data (recursively)
for /r "data" %%F in (*.asm) do (
  rem Build an output directory next to the file: <dir>\output\
  set "outdir=%%~dpFoutput\"
  if not exist "!outdir!" mkdir "!outdir!"

  rem Base name without extension
  set "base=!outdir!%%~nF"

  echo Processing: "%%F"
  nasm "%%F" -o "!base!"

  8086_decoder "!base!" > "!base!_decoded.output_asm"
  nasm "!base!_decoded.output_asm" -o "!base!_decoded"

  rem Binary compare
  fc /b "!base!_decoded" "!base!" >nul
  if errorlevel 1 (
    echo !RED!DIFF detected in "%%~F"!RESET!
    exit /b 1
  )
)

endlocal
