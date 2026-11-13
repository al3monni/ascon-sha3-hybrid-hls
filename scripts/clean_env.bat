@echo off
rem ============================================================================
rem clean_env.bat - Remove the AMD tool environment inherited from the caller
rem Location : <repo>\scripts\clean_env.bat   (called by the b04 launchers)
rem
rem [OPT] B-04: the launchers may be started from the Vivado Tcl console (exec): the child
rem       process then inherits RDI_* / XILINX_* / TCL_LIBRARY of that Vivado session.
rem       vitis-run.bat and vivado.bat set their own environment, so the inherited one is
rem       dropped first to keep every run as if launched from a fresh cmd window.
rem ============================================================================
for /f "tokens=1 delims==" %%A in ('set ^| findstr /b /i "RDI_ XILINX HDI_ TCL_LIBRARY TK_LIBRARY MYVIVADO"') do set "%%A="
exit /b 0
