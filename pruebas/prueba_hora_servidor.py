"""Valida que el servidor transmite instantes UNIX correctos en distintas zonas.
Ejecutar: python3 pruebas/prueba_hora_servidor.py ./servidor
"""
import datetime as dt
import os
import socket
import subprocess
import sys
import tempfile
import time
from zoneinfo import ZoneInfo

exe = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "./servidor")

def probar(tz):
    with tempfile.TemporaryDirectory() as tmp:
        sock = socket.socket()
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
        sock.close()
        env = os.environ.copy()
        env["TZ"] = tz
        servidor = subprocess.Popen([exe, str(port), os.path.join(tmp, "chat.db")],
                                    stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, env=env)
        conexion = None
        try:
            for _ in range(50):
                try:
                    conexion = socket.create_connection(("127.0.0.1", port), timeout=0.2)
                    break
                except OSError:
                    if servidor.poll() is not None:
                        raise RuntimeError("El servidor terminó antes de iniciar")
                    time.sleep(0.04)
            assert conexion, "El servidor no respondió"
            conexion.settimeout(2)
            arch = conexion.makefile("rwb", buffering=0)
            arch.write(b"REGISTRO|reloj|clave123|\n")
            assert arch.readline().startswith(b"OK|"), "Registro fallido"
            arch.write(b"GENERAL|comprobar hora\n")
            for _ in range(12):
                linea = arch.readline().decode("utf8")
                if linea.startswith("M|G|"):
                    campos = linea.strip().split("|", 6)
                    assert campos[-1] == "comprobar hora"
                    instante = int(campos[4])
                    assert abs(instante - time.time()) < 20, (tz, instante, time.time())
                    hora_local = dt.datetime.fromtimestamp(instante, ZoneInfo("America/Mexico_City"))
                    hora_utc = dt.datetime.fromtimestamp(instante, dt.timezone.utc)
                    print(f"{tz}: instante={instante}, México={hora_local:%H:%M}, UTC={hora_utc:%H:%M}: OK")
                    break
            else:
                raise AssertionError("No llegó mensaje M del servidor")
        finally:
            if conexion:
                conexion.close()
            servidor.terminate()
            try:
                servidor.wait(timeout=3)
            except subprocess.TimeoutExpired:
                servidor.kill()
                servidor.wait(timeout=3)

for zona in ("UTC", "America/Mexico_City"):
    probar(zona)
print("Prueba de hora del servidor OK")
