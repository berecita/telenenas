# TELECHAT — contraseñas y notificaciones (última entrega)

## Antes de usar esta versión

**Compila el servidor y el cliente de esta entrega.** La nueva comunicación de marcadores de lectura requiere ambas versiones actualizadas. En Windows, ejecuta `compilar_windows.cmd` para recompilar el cliente; en Linux, `make` para compilar el servidor.

### Si ya tenías usuarios registrados

Las contraseñas antiguas estaban guardadas mediante hash; **no se puede descubrir el texto original a partir de esos hashes**. Para no borrar usuarios, mensajes, grupos ni canales, esta actualización conserva las cuentas y deja temporalmente vacía su nueva contraseña en `Usuarios.clave`.

1. **Haz una copia** del `.db` que utilizabas y detén el servidor anterior.
2. Ejecuta una vez el servidor nuevo usando ese mismo `.db`. Lo migrará al esquema v3 y creará un respaldo automático con el sufijo `.respaldo_v2.db`.
3. Detén el servidor nuevo y ejecuta `python3 configurar_claves.py chat.db` (cambia `chat.db` por la ruta real). El script muestra las cuentas pendientes, solicita el nombre y pide una **contraseña nueva**. Repítelo para cada cuenta antigua.
4. Enciende otra vez el servidor e inicia sesión con la nueva contraseña.

El `chat-v2.db` incluido en este ZIP **ya se migró al esquema v3**, sin borrar sus cuentas, pero las que venían de la versión antigua requieren ejecutar el script para asignarles una contraseña.

### ¿Cómo funcionan ahora los puntos de aviso?

- Abres un chat: se guarda cuál fue el último mensaje que viste.
- Cierras e inicias sesión de nuevo: el historial ya leído no vuelve a producir un punto.
- Llega un mensaje nuevo de **otra persona**, incluso mientras estabas desconectado: aparece el punto hasta que abras esa conversación.
- Crear o cargar un grupo no enciende el punto, ni tampoco tus propios mensajes.

**Advertencia:** las contraseñas se guardan literalmente en SQLite y viajan sin cifrar por TCP. Usa cuentas y contraseñas de prueba; no reutilices claves reales. Base64 se conserva exclusivamente para transportar fotos por el protocolo de texto, no para contraseñas ni mensajes.

Para detalles y pruebas, consulta `README.md` y `CAMBIOS_REALIZADOS.md`.
