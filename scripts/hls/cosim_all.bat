@echo off
rem ============================================================================
rem cosim_all.bat - Throughput cosim (run_cosim.tcl) for all variants over a
rem                 list of already synthesized points, then collect.tcl
rem Location : <repo>\scripts\hls\cosim_all.bat
rem Usage    : scripts\hls\cosim_all.bat [T1 T2 ...]   (ns; default: 10 6.67 5 4)
rem Requires : points produced by run_point.tcl / sweep_hls.bat, and the
rem            throughput-mode main.c in the three variants
rem ============================================================================
setlocal

rem Adjust if Vitis is installed elsewhere
set "VITIS_RUN=C:\Xilinx\2025.1.1\Vitis\bin\vitis-run.bat"
if not exist "%VITIS_RUN%" (
    echo [ERROR] vitis-run not found: %VITIS_RUN%
    exit /b 1
)

set "GRID=%*"
if "%GRID%"=="" set "GRID=10 6.67 5 4"

rem Move to repo root (two levels above this script)
cd /d "%~dp0..\.."

for %%T in (%GRID%) do (
    for %%V in (Hybrid-Test Sha-Test Ascon-Test) do (
        echo.
        echo ===== cosim %%V @ %%T ns =====
        set "VARIANT=%%V"
        set "T_HLS=%%T"
        call "%VITIS_RUN%" --mode hls --tcl scripts\hls\run_cosim.tcl
    )
)

echo.
echo ########## collect ##########
call "%VITIS_RUN%" --mode hls --tcl scripts\hls\collect.tcl

endlocal
