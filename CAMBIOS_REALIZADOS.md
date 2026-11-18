# Cambios realizados en Telenenas

## Funciones nuevas

- **Agregar miembros:** el dueño abre **Administrar grupo** y escribe un usuario ya registrado, esté conectado o no. El usuario recibe el grupo y hasta 50 mensajes recientes, si está conectado. Si no está conectado, recibirá el grupo al iniciar sesión.
- **Quitar miembros:** solo el dueño puede expulsar integrantes. El dueño no puede autoexpulsarse.
- **Abandonar grupo:** cualquier integrante que no sea el creador puede pulsar **Salir del grupo**.
- **Eliminar grupo:** solo el dueño; se borran su historial y relaciones de miembros por `ON DELETE CASCADE`.
- **Eliminar canal:** solo el dueño; se elimina el canal, los mensajes y las suscripciones.
- **Eliminar cuenta:** hay confirmación por segunda pulsación, el servidor verifica la contraseña antes de borrar y se eliminan todas las salas creadas por esa cuenta. Las salas de otras personas permanecen.
- **Actualizaciones en directo:** `GRUPO_QUITADO`, `GRUPO_ELIMINADO`, `CANAL_ELIMINADO`, `CUENTA_BORRADA` y metadatos `GRUPO` actualizados. Los clientes eliminan las conversaciones y mensajes correspondientes de su vista.

## Archivos clave

- `src/comun/Protocolo.h`: nuevos comandos y eventos.
- `src/servidor/BaseDatos.h/.cpp`: SQL preparado, comprobación atómica de permisos y operaciones de borrado.
- `src/servidor/Servidor.h/.cpp`: validaciones, mensajes y actualizaciones en vivo.
- `src/cliente/Chat.cpp`: sincronización de eventos de eliminación.
- `src/cliente/mainCliente.cpp`: botones y ventanas modales con confirmación.
- `README.md`: instrucciones actualizadas.

## Comprobaciones

- `python3 pruebas/prueba_servidor.py ./servidor`: 61 comprobaciones originales superadas.
- `python3 pruebas/prueba_administracion.py ./servidor`: 45 comprobaciones nuevas superadas.
- Sintaxis de los archivos actualizados del cliente revisada con `clang++ -fsyntax-only` mediante declaraciones de SFML. **No sustituye una compilación y prueba gráfica con SFML real.**

## Compilación y uso

El ZIP incluye un servidor Linux recompilado y el código fuente de cliente actualizado. El entorno de preparación no tenía SFML ni conectividad para descargarla, de modo que **no contiene un cliente actualizado ya compilado**. Los ejecutables del cliente de la versión anterior se retiraron para evitar que se ejecute una versión obsoleta. En Windows, ejecuta `compilar_windows.cmd` tras instalar CMake, un compilador C++ compatible y Git; se descargará SFML 2.6.1 si no está instalada. Consulta `README.md`.

**Antes de eliminar cuentas o salas importantes, haz copia de la base de datos. El borrado es permanente.**

## Correcciones posteriores (TELECHAT)

- Título de la ventana y del acceso cambiado a **TELECHAT**.
- Botones **Administrar grupo** y **Eliminar canal** reubicados en el encabezado; textos acortados de manera segura para no taparlos.
- Eliminado el texto de contraseña duplicado en el diálogo de eliminación de cuenta; campo y etiqueta separados.
- Hora de mensajes corregida para respetar la zona horaria del cliente: servidor manda marca temporal UNIX a partir de las fechas locales que ya almacena SQLite, cliente la presenta en su zona horaria.
- Se deben recompilar cliente y servidor para que el ajuste de hora funcione. La base de datos existente no se modifica.

## Nuevas funciones: miembros de canales, borrado visible e historial completo

- **Ver miembros del canal:** el dueño dispone de un botón **Ver miembros** en el encabezado del canal, junto a **Eliminar canal**. Se abre una ventana modal con la lista completa, incluyendo usuarios desconectados; tiene desplazamiento por rueda. El servidor revisa la identidad de la sesión antes de divulgarla, y transmite los integrantes de uno en uno para no exceder el máximo de bytes por línea.
- **Mensajes eliminados:** el mensaje borrado se sustituye visualmente por `nombre_usuario eliminó un mensaje`, sin botón de borrado. Se mantiene una constancia en `MensajesEliminados`, con autor, ámbito, fecha e ID, **sin conservar el texto del mensaje**. Esto permite reconstruir el aviso tras reconectar. La tabla nueva se crea sin borrar ni convertir la base existente; su contenido se limpia en cascada cuando se elimina su usuario o sala.
- **Historial de canales sin tope:** servidor devuelve todos los mensajes al entrar a un canal o al iniciar sesión; el cliente conserva todas sus entradas y permite recorrerlas mediante rueda del mouse y las teclas `PgUp` / `PgDn` / `End`. El límite de lectura de 50 mensajes para general, directos y grupos se mantiene.
- **Compatibilidad:** se agregan las respuestas `CANAL_MIEMBROS_INICIO`, `CANAL_MIEMBRO`, `CANAL_MIEMBROS_FIN`, `MD` y el pedido `CANAL_MIEMBROS_PEDIR`. Se conserva el aviso existente `DEL` para no romper la comunicación en vivo. Para usar todas las mejoras deben actualizarse cliente y servidor.
- **Pruebas:** `pruebas/prueba_canales_historial.py` comprueba permisos, historial de 625 mensajes, borrados persistentes, mensajes directos con todo el contenido eliminado y ausencia de texto original en SQLite.

**Para actualizar tu aplicación:** vuelve a compilar el cliente y sustituye el servidor por el nuevo; conserva tu archivo `.db` habitual. Antes de cualquier actualización, respalda el archivo SQLite por precaución.
