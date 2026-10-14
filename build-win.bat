@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /O2 /I"ndi-sdk\include" /I"sdl2-vc\SDL2-2.32.10\include" /Fe:nornscope.exe src\nornscope.cpp "sdl2-vc\SDL2-2.32.10\lib\x64\SDL2.lib" ws2_32.lib || exit /b 1
cl /O2 /I"ndi-sdk\include" /I"sdl2-vc\SDL2-2.32.10\include" /Fe:ndi_grab.exe src\ndi_grab.cpp ws2_32.lib || exit /b 1
