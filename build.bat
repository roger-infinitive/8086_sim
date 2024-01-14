@echo off
call config.bat

setlocal

REM Configuration

REM Vars
set BUILD_DIR=build
set BUILD_FLAGS=/MD /O2 /GL /Ob2 /Oy
set LIB_PATH=libraries/x64
set EXE_NAME=8086_decoder
set OUTPUT_NAME=%BUILD_DIR%\%EXE_NAME%.exe

REM Check arguments
for %%a in (%*) do (
    if "%%a"=="Debug" (
    	set BUILD_FLAGS=/Od /Zi /MDd /D "_DEBUG"
		set PDB_PATH=%BUILD_DIR%\vc140.pdb
    	set LIB_PATH=libraries/debug/x64
    	set OUTPUT_NAME=%BUILD_DIR%\%EXE_NAME%_Debug.exe
    	echo Using DEBUG build
    )
)

REM Create the build directory if it doesn't exist
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

REM Detect Visual Studio's vcvarsall.bat
if not exist "%VCVARS_PATH%" (
	echo "Unable to find vcvarsall.bat. Please ensure Visual Studio is installed or update the VCVARS_PATH in this script."
	exit /b 1
)

REM Set up the Visual Studio envionment (for cl command)
call "%VCVARS_PATH%" x64

cl %BUILD_FLAGS% /I "include" /Fd"%PDB_PATH%" /Fo:%BUILD_DIR%/ /Fe:"%OUTPUT_NAME%" ^
src/main.cpp ^
/link /LIBPATH:"%LIB_PATH%"

@echo off

REM Check for compile errors
if errorlevel 1 (
	powershell -Command "Write-Host 'Compilation failed!' -ForegroundColor Red"
	exit /b 1
)

if "%~1" == "Debug" (
    powershell -Command "Write-Host 'Debug build complete!' -ForegroundColor Green"
) else (
	powershell -Command "Write-Host 'Release build complete!' -ForegroundColor Green"
)

endlocal