@echo off
setlocal EnableDelayedExpansion
rem get ESC for ANSI color (Win10+)
for /f "delims=" %%A in ('echo prompt $E^| cmd') do set "ESC=%%A"
set "RED=%ESC%[31m"
set "RESET=%ESC%[0m"

for /r "data" %%f in (*.asm) do (
  if "%%~xf"==".asm" (
    echo %%f
    nasm %%f
    8086_decoder %%~df%%~pf%%~nf > %%~df%%~pf%%~nf_decoded.output_asm
    nasm %%~df%%~pf%%~nf_decoded.output_asm
    fc %%~df%%~pf%%~nf_decoded %%~df%%~pf%%~nf >nul
    if errorlevel 1 (
      echo %RED%DIFF detected in %%~f%RESET%
      exit /b 1
    )
  )
)
endlocal
