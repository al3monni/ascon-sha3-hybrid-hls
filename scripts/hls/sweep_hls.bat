@echo off
rem ============================================================================
rem sweep_hls.bat - Phase 1 DSE: all variants over a grid of HLS clock targets,
rem                then collect.tcl -> results\hls\summary.csv
rem Location : <repo>\scripts\hls\sweep_hls.bat
rem Usage    : scripts\hls\sweep_hls.bat [T1 T2 ...]   (ns; default: 10 6.67 5 4)
rem Notes    : each point runs in a fresh vitis-run process (run_all.bat ->
rem            run_point.tcl), so a failing point does not stop the sweep.
rem            Existing points are re-run from scratch (open_component -reset).
rem ============================================================================
setlocal

rem Adjust if Vitis is installed elsewhere
set "VITIS_RUN=C:\Xilinx\2025.1.1\Vitis\bin\vitis-run.bat"
if not exist "%VITIS_RUN%" (
    echo [ERROR] vitis-run not found: %VITIS_RUN%
    exit /b 1
)

rem Grid of HLS clock targets [ns]: 100 / 150 / 200 / 250 MHz
set "GRID=%*"
if "%GRID%"=="" set "GRID=10 6.67 5 4"

rem Move to repo root (two levels above this script)
cd /d "%~dp0..\.."

for %%T in (%GRID%) do (
    echo.
    echo ########## T_HLS = %%T ns ##########
    call "%~dp0run_all.bat" %%T
)

echo.
echo ########## collect ##########
call "%VITIS_RUN%" --mode hls --tcl scripts\hls\collect.tcl

endlocal
