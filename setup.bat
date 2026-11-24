@echo off
rem GEX - refresh the bundled Dear ImGui (https://github.com/ocornut/imgui).
rem Usage: setup.bat [tag]   (default v1.93.0, use "master" for the latest)
setlocal
set TAG=%1
if "%TAG%"=="" set TAG=v1.93.0
where git >nul 2>nul || (echo git was not found in PATH. & exit /b 1)
set TMPDIR=%TEMP%\gex_imgui_%RANDOM%
git clone --depth 1 --branch %TAG% https://github.com/ocornut/imgui "%TMPDIR%" || exit /b 1
set DST=%~dp0third_party\imgui
if not exist "%DST%\backends" mkdir "%DST%\backends"
for %%f in (LICENSE.txt imconfig.h imgui.h imgui.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp imgui_internal.h imstb_rectpack.h imstb_textedit.h imstb_truetype.h) do copy /y "%TMPDIR%\%%f" "%DST%\" >nul
for %%f in (imgui_impl_win32.cpp imgui_impl_win32.h imgui_impl_dx11.cpp imgui_impl_dx11.h) do copy /y "%TMPDIR%\backends\%%f" "%DST%\backends\" >nul
rmdir /s /q "%TMPDIR%"
echo Dear ImGui %TAG% installed in third_party\imgui
