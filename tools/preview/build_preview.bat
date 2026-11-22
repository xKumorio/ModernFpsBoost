@echo off
rem ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
rem Copying, redistribution or reuse of this file without the author's permission is prohibited.
rem Builds build\preview\preview.exe: the menu drawn with made-up data, saved as PNGs (see preview.cpp).
rem Run build.bat first (it compiles imgui into build\imgui and the resources into build\obj\plugin.res).
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
cd /d "%~dp0..\.."
if not exist build\preview mkdir build\preview
set IMGUI=third_party\imgui
cl /nologo /c /O2 /MT /EHsc /W3 /std:c++20 /utf-8 /DNDEBUG /I%IMGUI% /Fobuild\preview\ ^
	tools\preview\preview.cpp src\core\*.cpp src\fixes\*.cpp src\fixes\gta\*.cpp src\fixes\samp\*.cpp src\fixes\external\*.cpp ^
	src\ui\menu.cpp src\ui\theme.cpp src\ui\render_dx9.cpp || exit /b 1
link /nologo /OUT:build\preview\preview.exe build\preview\*.obj build\imgui\*.obj build\obj\plugin.res ^
	user32.lib kernel32.lib shell32.lib ole32.lib windowscodecs.lib imm32.lib d3d9.lib || exit /b 1
echo OK: build\preview\preview.exe
