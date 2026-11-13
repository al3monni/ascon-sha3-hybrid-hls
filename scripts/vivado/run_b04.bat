@echo off
rem ============================================================================
rem run_b04.bat - B-04 Vivado flow (build_bd.tcl) for one control version, all (or some)
rem               variants: block design -> bitstream (.bit, .bit.bin) -> .xsa -> pl.dtsi
rem Location : <repo>\scripts\vivado\run_b04.bat
rem Usage    : scripts\vivado\run_b04.bat <lite|ib> [Hybrid-Test Sha-Test Ascon-Test]
rem Needs    : the HLS IP of each variant (scripts\hls\run_b04.bat). If the HLS batch of the
rem            same CTRL is still running, each variant waits for its run_point to finish
rem            (line "hls <ctrl> <VARIANT> run_point" in logs\b04\progress_<ctrl>.txt).
rem Output   : results\vivado\b04\<VARIANT>[-ib]\   logs\b04\vivado_<VARIANT>_<ctrl>.log
rem ============================================================================
setlocal EnableDelayedExpansion
call "%~dp0..\clean_env.bat"

rem Adjust if Vivado is installed elsewhere
set "VIVADO=C:\Xilinx\2025.1.1\Vivado\bin\vivado.bat"
if not exist "%VIVADO%" (
    echo [ERROR] vivado not found: %VIVADO%
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
if "%JOBS%"=="" set "JOBS=4"
if "%PL_MHZ%"=="" set "PL_MHZ=100"

for %%V in (%LIST%) do (
    set "VARIANT=%%V"
    call :wait_ip %%V
    echo [!date! !time!] vivado %CTRL% %%V start>> logs\b04\progress_vivado_%CTRL%.txt
    call "%VIVADO%" -mode batch -nojournal -nolog -notrace -source scripts\vivado\build_bd.tcl > "logs\b04\vivado_%%V_%CTRL%.log" 2>&1
    echo [!date! !time!] vivado %CTRL% %%V rc=!errorlevel!>> logs\b04\progress_vivado_%CTRL%.txt
)
echo [!date! !time!] vivado %CTRL% DONE>> logs\b04\progress_vivado_%CTRL%.txt
endlocal
exit /b 0

rem ---------------------------------------------------------------------------
rem wait_ip <VARIANT>: wait (polling every 60 s) for the HLS run_point of this variant,
rem only while the HLS batch of the same CTRL has a progress file (i.e. was started)
:wait_ip
if not exist "logs\b04\progress_%CTRL%.txt" exit /b 0
:wait_ip_loop
findstr /c:"hls %CTRL% %~1 run_point" "logs\b04\progress_%CTRL%.txt" > nul && exit /b 0
timeout /t 60 /nobreak > nul
goto wait_ip_loop
