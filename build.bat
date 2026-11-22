@echo off
rem ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
rem Copying, redistribution or reuse of this file without the author's permission is prohibited.
rem Builds "build\ModernFpsBoostBySoftikInquisitor(vk-miurawr).asi" (x86, VS 2022). "build.bat map" also writes a .map file.
setlocal

if defined VSCMD_VER goto :have_vs

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
	for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
)
if defined VSDIR if exist "%VSDIR%\VC\Auxiliary\Build\vcvars32.bat" goto :init_vs

for %%d in (
	"C:\Program Files\Microsoft Visual Studio\2022\Enterprise"
	"C:\Program Files\Microsoft Visual Studio\2022\Professional"
	"C:\Program Files\Microsoft Visual Studio\2022\Community"
	"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
) do (
	if exist "%%~d\VC\Auxiliary\Build\vcvars32.bat" (
		set "VSDIR=%%~d"
		goto :init_vs
	)
)

echo ERROR: Visual Studio 2022 C++ x86 tools not found.
exit /b 1

:init_vs
call "%VSDIR%\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1

:have_vs
cd /d "%~dp0"
if not exist build\obj mkdir build\obj
if not exist build\imgui mkdir build\imgui

set MAP=
if /i "%1"=="map" set MAP=/MAP:build\ModernFpsBoost.map
set IMGUI=third_party\imgui

rc /nologo /fo build\obj\plugin.res res\plugin.rc || exit /b 1
if not exist build\imgui\imgui.obj (
	cl /nologo /MP /c /O2 /MT /EHsc /W3 /std:c++20 /utf-8 /DNDEBUG /DNOMINMAX /I%IMGUI% /Fobuild\imgui\ ^
		%IMGUI%\imgui.cpp %IMGUI%\imgui_draw.cpp %IMGUI%\imgui_tables.cpp %IMGUI%\imgui_widgets.cpp %IMGUI%\backends\imgui_impl_win32.cpp || exit /b 1
)
cl /nologo /MP /c /O2 /MT /EHsc /W3 /std:c++20 /utf-8 /DNDEBUG /I%IMGUI% /Fobuild\obj\ ^
	src\main.cpp src\core\*.cpp src\fixes\*.cpp src\fixes\gta\*.cpp src\fixes\samp\*.cpp src\fixes\external\*.cpp src\profilers\*.cpp src\ui\*.cpp || exit /b 1
link /nologo /DLL "/OUT:build\ModernFpsBoostBySoftikInquisitor(vk-miurawr).asi" %MAP% build\obj\*.obj build\imgui\*.obj build\obj\plugin.res ^
	user32.lib kernel32.lib shell32.lib ole32.lib windowscodecs.lib imm32.lib || exit /b 1
echo OK: "build\ModernFpsBoostBySoftikInquisitor(vk-miurawr).asi"
