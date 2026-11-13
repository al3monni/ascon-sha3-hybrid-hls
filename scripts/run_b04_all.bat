@echo off
rem ============================================================================
rem run_b04_all.bat - B-04 complete flow for one control version: for each variant
rem                   HLS (run_point + run_cosim) and then Vivado (bitstream)
rem Location : <repo>\scripts\run_b04_all.bat
rem Usage    : scripts\run_b04_all.bat <lite|ib> [Hybrid-Test Sha-Test Ascon-Test]
rem            (two windows, one per CTRL, can run in parallel: 16 GB RAM are enough)
rem ============================================================================
setlocal
set "CTRL=%~1"
if "%CTRL%"=="" set "CTRL=lite"
set "LIST=%~2 %~3 %~4"
if "%~2"=="" set "LIST=Hybrid-Test Sha-Test Ascon-Test"
for %%V in (%LIST%) do (
    call "%~dp0hls\run_b04.bat" %CTRL% %%V
    call "%~dp0vivado\run_b04.bat" %CTRL% %%V
)
endlocal
