@echo off
rem ============================================================================
rem run_fmax.bat - LOG-016: Fmax search of the in-band IPs (fmax_search.tcl),
rem                one minimized console per variant, all in parallel
rem Location : <repo>\scripts\hls\run_fmax.bat
rem Usage    : scripts\hls\run_fmax.bat [Hybrid-Test Sha-Test Ascon-Test]   (default: all)
rem            optional environment: CTRL (default ib),
rem              F_LIST (default auto: Hybrid / SHA-3 200 225 250, Ascon 250 275 300 MHz),
rem              STOP_AT_FAIL (default 1), TIME_BUDGET_MIN (default 60), FALLBACK_STEP (25);
rem              F_LIST empty -> bisection (F_START, STEP_UP, RES, F_CAP, MAX_RUNS)
rem Progress : logs\fmax\progress_<VARIANT>-ib.txt   (one line per probe)
rem Results  : results\fmax\<design>\<VARIANT>-ib\   (points.csv, fmax.txt, F_<MHz>\)
rem ============================================================================
setlocal
call "%~dp0..\clean_env.bat"

rem Adjust if Vitis is installed elsewhere
set "VITIS_RUN=C:\Xilinx\2025.1.1\Vitis\bin\vitis-run.bat"
if not exist "%VITIS_RUN%" (
    echo [ERROR] vitis-run not found: %VITIS_RUN%
    exit /b 1
)

cd /d "%~dp0..\.."
if not exist logs\fmax mkdir logs\fmax
if "%CTRL%"=="" set "CTRL=ib"
rem [OPT] LOG-016: fixed grid per variant (25 MHz step, 3 points), stop at the first point that
rem       misses timing, whole search bounded to about one hour
rem [WAS] if not defined F_LIST set "F_LIST=200 225 250 275 300"
if not defined F_LIST set "F_LIST=auto"
if not defined TIME_BUDGET_MIN set "TIME_BUDGET_MIN=60"
if "%STOP_AT_FAIL%"=="" set "STOP_AT_FAIL=1"
set "DESIGN="
set "LIST=%*"
if "%~1"=="" set "LIST=Hybrid-Test Sha-Test Ascon-Test"

rem start inherits the environment at the time it runs: VARIANT is set right before each start
for %%V in (%LIST%) do (
    set "VARIANT=%%V"
    start "fmax %%V" /min cmd /c "%VITIS_RUN% --mode hls --tcl scripts\hls\fmax_search.tcl > logs\fmax\search_%%V.log 2>&1"
    echo started: fmax %%V ^(progress in logs\fmax\progress_*.txt^)
)
endlocal
