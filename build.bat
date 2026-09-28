@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist build mkdir build
cl /nologo /O2 /EHsc /I third_party src\*.cpp /Fe:build\desktop-fx.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib kernel32.lib d3d11.lib dxgi.lib dxguid.lib d3dcompiler.lib
if %ERRORLEVEL% equ 0 (
    echo Build succeeded: build\desktop-fx.exe
) else (
    echo Build failed
)
