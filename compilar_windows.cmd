@echo off
setlocal
cd /d "%~dp0"
echo Compilando el cliente actualizado (requiere CMake, C++ y SFML 2.x / Internet)...
cmake -S . -B build-windows -DBUILD_SERVIDOR=OFF
if errorlevel 1 goto error
cmake --build build-windows --config Release --parallel 4
if errorlevel 1 goto error
echo.
echo Compilacion terminada. Busca cliente.exe en build-windows\Release o build-windows\
pause
exit /b 0
:error
echo.
echo Error al compilar. Revisa el README y las dependencias.
pause
exit /b 1
