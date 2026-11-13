@echo off
rem ============================================================================
rem run_b04.bat - B-04 HLS flow for one control version, all (or some) variants:
rem               run_point.tcl (csim, csynth, cosim KAT, export IP + OOC impl)
rem               -> run_cosim.tcl (robust + throughput) -> collect.tcl
rem Location : <repo>\scripts\hls\run_b04.bat
rem Usage    : scripts\hls\run_b04.bat <lite|ib> [Hybrid-Test Sha-Test Ascon-Test]
rem Output   : build\hls\b04\<VARIANT>[-ib]\T_10\   (IP for Vivado: hls\impl\ip\)
rem            results\hls\b04\                      (collect.tcl)
rem            logs\b04\hls_<VARIANT>_<ctrl>.log + logs\b04\progress_<ctrl>.txt
rem ============================================================================
setlocal EnableDelayedExpansion
call "%~dp0..\clean_env.bat"

rem Adjust if Vitis is installed elsewhere
set "VITIS_RUN=C:\Xilinx\2025.1.1\Vitis\bin\vitis-run.bat"
if not exist "%VITIS_RUN%" (
    echo [ERROR] vitis-run not found: %VITIS_RUN%
    exit /b 1
)

set "CTRL=%~1"
if "%CTRL%"=="" set "CTRL=lite"
if /i not "%CTRL%"=="lite" if /i not "%CTRL%"=="ib" (
    echo [ERROR] CTRL must be lite or ib: %CTRL%
    exit /b 1
)
rem optional variant list (up to 3), default: all
set "LIST=%~2 %~3 %~4"
if "%~2"=="" set "LIST=Hybrid-Test Sha-Test Ascon-Test"

rem Move to repo root (two levels above this script)
cd /d "%~dp0..\.."
if not exist logs\b04 mkdir logs\b04
set "T_HLS=10"
set "DESIGN="

for %%V in (%LIST%) do (
    set "VARIANT=%%V"
    set "LOG=logs\b04\hls_%%V_%CTRL%.log"
    echo [!date! !time!] hls %CTRL% %%V start>> logs\b04\progress_%CTRL%.txt

    call "%VITIS_RUN%" --mode hls --tcl scripts\hls\run_point.tcl > "!LOG!" 2>&1
    echo [!date! !time!] hls %CTRL% %%V run_point rc=!errorlevel!>> logs\b04\progress_%CTRL%.txt

    rem robust (wrong DMA length) + throughput vectors, one cosim per argument
    if "%%V"=="Hybrid-Test" set "ALGOS=robust sha3-256 sha3-512 ascon"
    if "%%V"=="Sha-Test"    set "ALGOS=robust sha3-256 sha3-512"
    if "%%V"=="Ascon-Test"  set "ALGOS=robust ascon"
    call "%VITIS_RUN%" --mode hls --tcl scripts\hls\run_cosim.tcl >> "!LOG!" 2>&1
    echo [!date! !time!] hls %CTRL% %%V run_cosim rc=!errorlevel!>> logs\b04\progress_%CTRL%.txt
    set "ALGOS="
)

call "%VITIS_RUN%" --mode hls --tcl scripts\hls\collect.tcl > "logs\b04\collect_%CTRL%.log" 2>&1
echo [!date! !time!] hls %CTRL% collect rc=!errorlevel! DONE>> logs\b04\progress_%CTRL%.txt
endlocal
