echo === WiChat Build ===
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >/dev/null 2>&1
if not exist build mkdir build
echo Compiling...
cl /nologo /O2 /W3 /I src /I lib /I lib\libusb /I lib\tinycrypt /D WIN32 /D _WINDOWS /D UNICODE /D _CRT_SECURE_NO_WARNINGS src\main.c src\frame.c src\mesh.c src\wspr.c src\crypto.c src\radio_rtl8811au.c src\gui.c lib\tinycrypt\aes_encrypt.c lib\tinycrypt\ccm_mode.c lib\tinycrypt\utils.c /Fe:build\wichat.exe /link user32.lib gdi32.lib msimg32.lib lib\libusb\libusb-1.0.lib
if errorlevel 1 goto fail
copy /Y lib\libusb-1.0.dll build\ >/dev/null 2>&1
echo Build OK: build\wichat.exe
goto end
:fail
echo Build FAILED
:end
