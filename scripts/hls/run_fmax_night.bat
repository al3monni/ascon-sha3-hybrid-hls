@echo off
rem ============================================================================
rem run_fmax_night.bat - LOG-016: unattended HLS clock sweep + Fmax search of ONE variant
rem   (default Hybrid-Test, in-band IP), meant to run overnight
rem Location : <repo>\scripts\hls\run_fmax_night.bat
rem Usage    : scripts\hls\run_fmax_night.bat        (from any folder; no argument)
rem
rem   Phase 1 (Vitis, the RTL changes), sequential, for every T in NIGHT_GRID:
rem     run_point.tcl   csim, csynth @ T, KAT cosim, Vivado OOC impl @ T
rem     run_cosim.tcl   throughput cosim, one vitis-run per configuration of NIGHT_ALGOS
rem     -> collect.tcl  results\hls\<design>\summary.csv (f_impl = first Fmax probe)
rem   Phase 2 (Vivado, fixed RTL), fmax_search.tcl in bisection mode on every RTL:
rem     NIGHT_WAVE1 searches in parallel, then NIGHT_WAVE2 searches in parallel
rem   Analysis: collect.tcl, tp_analysis.tcl -> results\fmax\<design>\<VARIANT>-ib\tp_sweep.*
rem
rem   Restartable: points, cosim reports and Fmax probes already present are skipped
rem   (fmax_search.tcl resumes from points.csv). A failing step is logged, the night goes on.
rem   No goto / call :label: safe with LF line endings (git checkout from WSL).
rem
rem Optional environment (defaults in brackets):
rem   NIGHT_VARIANT   [Hybrid-Test]
rem   NIGHT_GRID      [10 8 6.67 5 4 3.33]           HLS clock targets [ns], phase 1
rem   NIGHT_ALGOS     [sha3-256 sha3-512 ascon sha3-256v sha3-512v]   throughput cosim
rem   NIGHT_WAVE1     [10 8 6.67]   NIGHT_WAVE2 [5 4 3.33]           phase 2, parallel waves ("-" = none)
rem   NIGHT_MAX_RUNS  [12]          new probes per Fmax search (MAX_RUNS of fmax_search.tcl)
rem   NIGHT_WAIT_MIN  [720]         longest wait for one wave [min]
rem
rem Progress : logs\fmax_night\progress.txt   (one line per step, with time)
rem Logs     : logs\fmax_night\point_T_*.log, cosim_T_*_<algo>.log, fmax_T_*.log, collect*.log
rem            logs\fmax\progress_<VARIANT>-ib_T_*.txt   (one line per Fmax probe)
rem ============================================================================
setlocal EnableDelayedExpansion
call "%~dp0..\clean_env.bat"
rem every vitis-run runs in its own cmd with delayed expansion OFF (cmd /v:off): the AMD .bat
rem launchers must not inherit the EnableDelayedExpansion of this script

rem Adjust if Vitis is installed elsewhere
set "VITIS_RUN=C:\Xilinx\2025.1.1\Vitis\bin\vitis-run.bat"
if not exist "%VITIS_RUN%" (
    echo [ERROR] vitis-run not found: %VITIS_RUN%
    exit /b 1
)

rem Move to repo root (two levels above this script)
cd /d "%~dp0..\.."

if not defined NIGHT_VARIANT  set "NIGHT_VARIANT=Hybrid-Test"
if not defined NIGHT_GRID     set "NIGHT_GRID=10 8 6.67 5 4 3.33"
if not defined NIGHT_ALGOS    set "NIGHT_ALGOS=sha3-256 sha3-512 ascon sha3-256v sha3-512v"
if not defined NIGHT_WAVE1    set "NIGHT_WAVE1=10 8 6.67"
if not defined NIGHT_WAVE2    set "NIGHT_WAVE2=5 4 3.33"
if not defined NIGHT_MAX_RUNS set "NIGHT_MAX_RUNS=12"
if not defined NIGHT_WAIT_MIN set "NIGHT_WAIT_MIN=720"
rem "-" = empty wave (e.g. a short test on T_10 only: NIGHT_GRID=10, NIGHT_WAVE1=10, NIGHT_WAVE2=-)
if "%NIGHT_WAVE1%"=="-" set "NIGHT_WAVE1="
if "%NIGHT_WAVE2%"=="-" set "NIGHT_WAVE2="

rem Environment read by the Tcl scripts (common.tcl, run_point, run_cosim, fmax_search)
set "VARIANT=%NIGHT_VARIANT%"
set "CTRL=ib"
set "DESIGN="
set "STEPS="
set "ALGOS="
set "F_VIVADO="
rem fmax_search.tcl: bisection (empty F_LIST), resume on, 25 MHz up then 5 MHz resolution
set "F_LIST="
set "F_START="
set "RESUME=1"
set "STEP_UP=25"
set "RES=5"
set "MAX_RUNS=%NIGHT_MAX_RUNS%"

rem Design label: first non-comment line of scripts\hls\design.txt
set "DLABEL="
for /f "usebackq eol=# tokens=* delims=" %%L in ("scripts\hls\design.txt") do (
    if not defined DLABEL set "DLABEL=%%L"
)
if not defined DLABEL (
    echo [ERROR] no design label in scripts\hls\design.txt
    exit /b 1
)
set "BASE=build\hls\%DLABEL%\%VARIANT%-ib"
set "NL=logs\fmax_night"
if not exist "%NL%" mkdir "%NL%"
set "PROG=%NL%\progress.txt"
rem 30 s per poll
set /a POLLS=%NIGHT_WAIT_MIN% * 2

echo [!date! !time!] ===== night start: %DLABEL% %VARIANT%-ib, grid %NIGHT_GRID% ns =====>>"%PROG%"
echo Progress in %CD%\%PROG%

rem ---------------------------------------------------------------------------
rem Phase 1: HLS points + throughput cosim (sequential)
rem ---------------------------------------------------------------------------
for %%T in (%NIGHT_GRID%) do (
    set "TT=%%T"
    set "TAG=T_!TT:.=p!"
    set "PT=%BASE%\!TAG!"
    set "T_HLS=%%T"
    if exist "!PT!\hls\impl\report\verilog\export_impl.rpt" (
        echo [!date! !time!] phase 1 !TAG!: point already done, run_point skipped>>"%PROG%"
    ) else (
        echo [!date! !time!] phase 1 !TAG!: run_point start>>"%PROG%"
        set "STEPS="
        cmd /d /v:off /c "%VITIS_RUN% --mode hls --tcl scripts\hls\run_point.tcl" > "%NL%\point_!TAG!.log" 2>&1
        if exist "!PT!\hls\impl\report\verilog\export_impl.rpt" (
            echo [!date! !time!] phase 1 !TAG!: run_point done>>"%PROG%"
        ) else (
            echo [!date! !time!] phase 1 !TAG!: run_point FAILED, see %NL%\point_!TAG!.log>>"%PROG%"
        )
    )
    if exist "!PT!\hls\syn\report\csynth.rpt" (
        for %%A in (%NIGHT_ALGOS%) do (
            if exist "!PT!\cosim\%%A.rpt" (
                echo [!date! !time!] phase 1 !TAG!: cosim %%A already done>>"%PROG%"
            ) else (
                set "ALGOS=%%A"
                cmd /d /v:off /c "%VITIS_RUN% --mode hls --tcl scripts\hls\run_cosim.tcl" > "%NL%\cosim_!TAG!_%%A.log" 2>&1
                if exist "!PT!\cosim\%%A.rpt" (
                    echo [!date! !time!] phase 1 !TAG!: cosim %%A done>>"%PROG%"
                ) else (
                    echo [!date! !time!] phase 1 !TAG!: cosim %%A FAILED, see %NL%\cosim_!TAG!_%%A.log>>"%PROG%"
                )
                set "ALGOS="
            )
        )
    )
)

rem summary.csv must hold f_impl of the new points: first probe of each Fmax search
echo [!date! !time!] collect>>"%PROG%"
cmd /d /v:off /c "%VITIS_RUN% --mode hls --tcl scripts\hls\collect.tcl" > "%NL%\collect_phase1.log" 2>&1

rem ---------------------------------------------------------------------------
rem Phase 2: Fmax search on every RTL, two waves of parallel searches
rem   (start inherits the environment at the time it runs: T_HLS is set right before)
rem ---------------------------------------------------------------------------
for %%W in (1 2) do (
    set "PENDING="
    for %%T in (!NIGHT_WAVE%%W!) do (
        set "TT=%%T"
        set "TAG=T_!TT:.=p!"
        if exist "%BASE%\!TAG!\hls\syn\verilog" (
            if exist "%NL%\done_!TAG!.flag" del /q "%NL%\done_!TAG!.flag"
            set "T_HLS=%%T"
            start "fmax !TAG!" /min cmd /d /v:off /c "call %VITIS_RUN% --mode hls --tcl scripts\hls\fmax_search.tcl > %NL%\fmax_!TAG!.log 2>&1 & echo done> %NL%\done_!TAG!.flag"
            set "PENDING=!PENDING! %NL%\done_!TAG!.flag"
            echo [!date! !time!] phase 2 wave %%W: Fmax search !TAG! started>>"%PROG%"
        ) else (
            echo [!date! !time!] phase 2 wave %%W: !TAG! skipped, no csynth RTL>>"%PROG%"
        )
    )
    rem wait for every search of the wave: bounded polling loop, no goto
    set "WDONE=0"
    if not defined PENDING set "WDONE=1"
    for /l %%i in (1,1,%POLLS%) do (
        if "!WDONE!"=="0" (
            set "ALL=1"
            for %%F in (!PENDING!) do if not exist "%%F" set "ALL=0"
            if "!ALL!"=="1" (
                set "WDONE=1"
            ) else (
                ping -n 31 127.0.0.1 >nul
            )
        )
    )
    if "!WDONE!"=="1" (
        echo [!date! !time!] phase 2 wave %%W: done>>"%PROG%"
    ) else (
        echo [!date! !time!] phase 2 wave %%W: TIMEOUT after %NIGHT_WAIT_MIN% min, going on>>"%PROG%"
    )
)

rem ---------------------------------------------------------------------------
rem Analysis
rem ---------------------------------------------------------------------------
cmd /d /v:off /c "%VITIS_RUN% --mode hls --tcl scripts\hls\collect.tcl" > "%NL%\collect_final.log" 2>&1
cmd /d /v:off /c "%VITIS_RUN% --mode hls --tcl scripts\hls\tp_analysis.tcl" > "%NL%\tp_analysis.log" 2>&1
echo [!date! !time!] ===== night done: results\fmax\%DLABEL%\%VARIANT%-ib\tp_sweep.txt =====>>"%PROG%"
echo Done: results\fmax\%DLABEL%\%VARIANT%-ib\tp_sweep.txt
endlocal