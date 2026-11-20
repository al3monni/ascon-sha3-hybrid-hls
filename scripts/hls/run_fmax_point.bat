@echo off
rem ============================================================================
rem run_fmax_point.bat - LOG-016: one probe of the Fmax search
rem   csynth at T_HLS (fixed RTL) + export_design -flow impl with the Vivado OOC clock at
rem   <MHz> (run_point.tcl, F_VIVADO, STEPS=impl: no csim / cosim)
rem Location : <repo>\scripts\hls\run_fmax_point.bat
rem Usage    : run_fmax_point.bat <VARIANT> <MHz> <lite|ib> <T_HLS> <log>
rem            (called by fmax_search.tcl, not meant to be run by hand)
rem [OPT] LOG-016: one folder per fixed RTL (T_<T_HLS>), see run_point.tcl
rem Output   : build\fmax\<design>\<VARIANT>[-ib]\T_<ns>\F_<MHz>\
rem ============================================================================
setlocal
rem [OPT] fmax_search.tcl runs inside vitis-run: drop its environment, as for every b04 run
call "%~dp0..\clean_env.bat"

rem Adjust if Vitis is installed elsewhere
set "VITIS_RUN=C:\Xilinx\2025.1.1\Vitis\bin\vitis-run.bat"

cd /d "%~dp0..\.."
set "VARIANT=%~1"
set "F_VIVADO=%~2"
set "CTRL=%~3"
set "T_HLS=%~4"
set "STEPS=impl"
set "DESIGN="
call "%VITIS_RUN%" --mode hls --tcl scripts\hls\run_point.tcl > "%~5" 2>&1
set "RC=%errorlevel%"
endlocal & exit /b %RC%