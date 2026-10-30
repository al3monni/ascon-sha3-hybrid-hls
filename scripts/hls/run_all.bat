@echo off
rem ============================================================================
rem run_all.bat - Run scripts\hls\run_point.tcl on all three accelerators
rem Location : <repo>\scripts\hls\run_all.bat
rem Usage    : scripts\hls\run_all.bat [T_HLS_ns]      (default: 10)
rem ============================================================================
setlocal

rem Adjust if Vitis is installed elsewhere
set "VITIS_RUN=C:\Xilinx\2025.1.1\Vitis\bin\vitis-run.bat"
if not exist "%VITIS_RUN%" (
    echo [ERROR] vitis-run not found: %VITIS_RUN%
    exit /b 1
)

set "T_HLS=10"
if not "%~1"=="" set "T_HLS=%~1"

rem Move to repo root (two levels above this script)
cd /d "%~dp0..\.."

for %%V in (Hybrid-Test Sha-Test Ascon-Test) do (
    echo.
    echo ===== %%V @ %T_HLS% ns =====
    set "VARIANT=%%V"
    call "%VITIS_RUN%" --mode hls --tcl scripts\hls\run_point.tcl
)

endlocal
