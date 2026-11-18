# TELECHAT (C++) 

Sistema de mensajería instantánea hecho con **sockets TCP** en C++17.

- **Servidor** (sin interfaz gráfica): gestiona usuarios, guarda todo en una base de datos SQLite y propaga los mensajes. Corre en Linux (probado para Debian).
- **Cliente** (interfaz gráfica con **SFML**): corre en **Windows, GNU/Linux y macOS**. La red del cliente usa `sf::TcpSocket`, así que el mismo código sirve en los tres sistemas.

No usa páginas web ni websockets: es TCP puro con un protocolo de texto propio (ver [Protocolo](#protocolo)).

## Funciones

| Sección | Qué hace |
|---|---|
| **Cuenta** | Registro e inicio de sesión con usuario, contraseña y foto de perfil: un **avatar de animalito** (gato, perro, conejo, oso o zorro) o una **foto propia**; ambas son opcionales. |
| **General** | Chat para todos los conectados; cada mensaje muestra el nombre y la foto de quien lo envía. |
| **Directos** | Eliges a un usuario y chateas en privado. Si no está conectado, recibe el mensaje al volver. |
| **Grupos** | Creas un grupo con nombre y eliges a los miembros conectados; sus mensajes solo los ven ellos. El creador puede añadir miembros existentes (aunque estén desconectados), quitar miembros y eliminar el grupo. Otros miembros pueden abandonarlo. |
| **Eliminar** | Puedes eliminar tus propios mensajes en cualquier conversación (se borran para todos y de la base de datos). |
| **Canales** | Creas un canal de difusión (queda registrado en el servidor). Cualquiera puede ver la lista y unirse: al unirse ve **todo el historial** pero **solo el dueño puede escribir**. El creador puede eliminarlo definitivamente. |

El historial de todas las conversaciones se guarda en la base de datos y se recupera al iniciar sesión.

- **Eliminar cuenta:** cada usuario puede borrar su propia cuenta tras confirmar su contraseña. Se borran sus mensajes, grupos y canales creados (con sus historias y membresías); los grupos y canales de otras personas permanecen.
- **Las eliminaciones y los cambios de miembros se comunican a los clientes conectados** y se reflejan en SQLite; no hace falta reiniciar sesión.

## Estructura

```
src/comun/      Protocolo.h (formato de mensajes) y Base64
src/servidor/   mainServidor, Servidor (lógica), BaseDatos (SQLite)
src/cliente/    mainCliente (pantallas), Chat (estado), Conexion (red), Avatares, Ventana, Widgets
recursos/       font.ttf (DejaVu Sans, ver LICENSE-DejaVu.txt) y avatares/ (5 animalitos .png)
pruebas/        prueba_servidor.py (prueba de integración del servidor)
```

## Compilar y ejecutar el servidor (Debian/Ubuntu)

Requisitos: `g++` y SQLite con cabeceras.

```bash
sudo apt install build-essential libsqlite3-dev    # (sin sudo: ver nota)
make                                               # genera ./servidor
./servidor 5123 chat.db                            # puerto asignado y archivo de base de datos
```

- El primer argumento es **el puerto asignado al equipo**; el segundo, el archivo de la base de datos (se crea solo; por defecto `chat.db`, puerto `5000`).
- Detener con `Ctrl+C`.
- También se puede compilar con CMake: `cmake -S . -B build && cmake --build build` (deja `build/servidor`).
- *Sin permisos para instalar paquetes:* descarga el "amalgamation" de SQLite desde sqlite.org y copia `sqlite3.c` y `sqlite3.h` en `third_party/sqlite3/`; CMake los usa automáticamente.

**Base de datos.** Se usa SQLite (archivo local, sin cuentas ni configuración). Todo el SQL está aislado en `src/servidor/BaseDatos.cpp`; si hiciera falta otro motor (MySQL, PostgreSQL), solo se reescribe ese archivo.

Esquema (tipos de datos estándar; las llaves foráneas están activadas):

| Tabla | Columnas principales |
|---|---|
| `Usuarios` | `idUsuario`, `nombre VARCHAR(16)`, `clave TEXT` (texto literal), `lecturasInicializadas`, `avatar`, `foto`, `fechaRegistro DATETIME` |
| `Mensajes` | `idMensaje`, `tipo` (G/D/R/C), `idAutor`, `idDestino`, `idGrupo`, `idCanal`, `texto VARCHAR(500)`, `fechaEnvio DATETIME` |
| `MensajesEliminados` | `idMensaje`, `tipo`, `idAutor`, `idDestino`, `idGrupo`, `idCanal`, `fechaEnvio` (sin texto original). |
| `Lecturas` | `idUsuario`, `ambito`, `clave`, `ultimoId`: último mensaje leído de cada conversación, guardado entre sesiones. |
| `Grupos` / `GrupoMiembros` | `idGrupo`, `nombre`, `idDueno`, `fechaCreacion` / `idGrupo`, `idUsuario`, `fechaUnion` |
| `Canales` / `CanalMiembros` | `idCanal`, `nombre` (único), `idDueno`, `fechaCreacion` / `idCanal`, `idUsuario`, `fechaUnion` |

Las fechas se mantienen en SQLite como `DATETIME` en texto `AAAA-MM-DD HH:MM:SS` (hora local del servidor), sin migrar la base existente. Al transmitir mensajes, el servidor convierte esa fecha a una marca UNIX; **el cliente muestra la hora local de su computadora**, aunque el servidor esté en otra zona horaria. Para que aparezca la hora correcta hay que recompilar y ejecutar **servidor y cliente** de esta versión:

```bash
sqlite3 -header -column chat.db "SELECT m.idMensaje, u.nombre AS autor, m.texto, m.fechaEnvio FROM Mensajes m JOIN Usuarios u ON u.idUsuario = m.idAutor;"
```

### Actualizar una base de datos anterior (muy importante)

**No borres tu `.db` ni sus mensajes.** La versión anterior guardaba claves mediante SHA-256, por lo que **no es posible recuperar las contraseñas originales**. Esta actualización usa el esquema v3: almacena la contraseña tal como se escribió, sin hash ni sal.

1. Cierra el servidor anterior y haz una copia de seguridad de tu archivo de base de datos.
2. Ejecuta una vez **el servidor nuevo** apuntando al mismo `.db`. Si es el esquema v2, actualizará la tabla de usuarios y guardará automáticamente un respaldo llamado `chat.db.respaldo_v2.db` (ajustando el nombre al archivo usado).
3. Cierra el servidor. Las cuentas anteriores se conservan, con sus grupos y mensajes, pero quedan **sin contraseña activa** hasta que les asignes una nueva. Ejecuta `python3 configurar_claves.py chat.db` y escribe el nombre de cada cuenta antigua y su nueva contraseña (sin hash). Repite el comando para las demás cuentas pendientes.
4. Vuelve a iniciar el servidor y recompila el cliente actualizado.

El script también permite cambiar la contraseña de una cuenta ya migrada. Una base de datos nueva usa el esquema v3 directamente; las cuentas nuevas no necesitan este paso. Esquemas anteriores al v2 siguen sin ser compatibles.

**Aviso:** las contraseñas quedan legibles dentro del archivo SQLite, y este chat utiliza TCP sin TLS. Usa contraseñas de prueba y no reutilices contraseñas personales.

## Compilar y ejecutar el cliente

Requisitos: CMake ≥ 3.16 y un compilador C++17. Si no hay **SFML 2.5/2.6** instalada, CMake la descarga y compila sola (necesita internet la primera vez y `git`).

```bash
cmake -S . -B build -DBUILD_SERVIDOR=OFF
cmake --build build --config Release
```

**Importante sobre los ejecutables:** esta entrega contiene el servidor recompilado para **Linux** y el **código fuente actualizado del cliente**. Los ejecutables antiguos del cliente (incluido el `.exe` de Windows de la entrega original) fueron retirados para evitar que se abra por error una versión sin las nuevas funciones. Para Windows puedes ejecutar `compilar_windows.cmd` desde una terminal con CMake y un compilador C++ instalados; el primer compilado requiere Internet para obtener SFML 2.6.1 si no la tienes instalada.

**Dependencias del sistema**

- **Linux:** `sudo apt install cmake build-essential git libx11-dev libxrandr-dev libxcursor-dev libudev-dev libfreetype-dev libgl1-mesa-dev` (o `libsfml-dev` si prefieres la SFML del sistema, versión 2.x).
- **macOS:** `brew install cmake` (Xcode Command Line Tools incluidas). Con `brew install sfml@2` se evita la descarga.
- **Windows:** Visual Studio (Escritorio con C++) o MSYS2/MinGW, más CMake y Git.

Ejecutar (el ejecutable queda en `build/` o en `build/Release/`):

```bash
./build/cliente                    # IP y puerto se escriben en la pantalla de login
./build/cliente 10.0.0.5 5123      # o se pasan como argumentos
```

## Uso

1. En la pantalla de entrada escribe la **IP del servidor** y el **puerto** de tu equipo.
2. **Registrarme**: usuario (máx. 16 caracteres), contraseña (mín. 4) y, si quieres, un perfil: **haz clic en uno de los 5 animalitos** (otro clic lo quita) o escribe la **ruta de una imagen propia** (png, jpg, bmp, tga), que se recorta a cuadrado y se reduce a 64×64. Si pones ruta, esa foto tiene prioridad. En Windows puedes usar «Copiar como ruta» y pegar con `Ctrl+V`.
3. **Entrar**: usuario y contraseña (la foto ya no hace falta).
4. Usa las secciones de la izquierda. `Enter` envía; rueda del mouse, `PgUp/PgDn` y `End` mueven el historial.
5. **Eliminar un mensaje tuyo:** pasa el mouse sobre su nombre y pulsa «eliminar»; pide confirmación con un segundo clic («¿seguro?»). El contenido desaparece y se muestra **«usuario eliminó un mensaje»** a los participantes. La constancia también se ve al iniciar sesión otra vez; nunca almacena el contenido borrado.
6. **Administrar un grupo:** entra a **Grupos**, selecciona uno y pulsa **Administrar grupo** en la esquina superior derecha del encabezado. El propietario puede escribir el nombre de un usuario en **Agregar** (aunque esté desconectado), o pulsar **Quitar** junto a un miembro. Los demás integrantes pueden pulsar **Salir del grupo**. Usa la rueda del mouse para desplazarte si hay más de cinco integrantes.
7. **Eliminar un grupo:** el creador abre **Administrar grupo** y pulsa **Eliminar grupo**; la aplicación solicita confirmación. El creador no puede abandonar su propio grupo: debe eliminarlo.
8. **Ver integrantes de un canal:** su creador selecciona el canal y pulsa **Ver miembros** en la esquina superior derecha. La lista incluye a quienes están desconectados y se puede recorrer con la rueda del mouse. Solo el dueño puede solicitarla; otros usuarios no ven ni reciben estos nombres.
9. **Eliminar un canal:** el creador selecciona su canal y pulsa **Eliminar canal**; la aplicación solicita confirmación.
10. **Eliminar tu cuenta:** pulsa **Eliminar cuenta** en la barra rosa izquierda. Ingresa tu contraseña, pulsa **Eliminar mi cuenta** y confirma pulsando de nuevo. Al confirmarlo el servidor, vuelves a la pantalla de acceso. **Esta operación es irreversible.**

## Protocolo

Un mensaje es una línea de texto terminada en `\n`, con campos separados por `|` (el último campo puede llevar `|`). La referencia completa está en `src/comun/Protocolo.h`. En **Canales**, tanto al unirte como al volver a entrar se carga **todo el historial disponible**, sin los topes anteriores de 50 mensajes del servidor ni 600 del cliente. Los demás chats conservan su ventana de historial habitual de 50 mensajes de carga inicial.

| Cliente → servidor | Servidor → cliente |
|---|---|
| `REGISTRO\|usuario\|clave\|foto` / `LOGIN\|usuario\|clave` | `OK\|usuario` / `ERR\|motivo` |
| `GENERAL\|texto`, `DM\|destino\|texto` | `M\|ámbito\|clave\|id\|hora\|autor\|texto` |
| `GRUPO_NUEVO\|nombre\|m1,m2`, `GMSG\|id\|texto` | `GRUPO\|id\|nombre\|dueño\|miembros` |
| `GRUPO_AGREGAR\|id\|usuario`, `GRUPO_QUITAR\|id\|usuario`, `GRUPO_SALIR\|id`, `GRUPO_ELIMINAR\|id` | `GRUPO_QUITADO\|id`, `GRUPO_ELIMINADO\|id`, `GRUPO\|...` (lista actualizada) |
| `CANAL_NUEVO\|nombre`, `CANAL_UNIR\|id`, `CMSG\|id\|texto` | `CANAL\|id\|nombre\|dueño\|unido` |
| `CANAL_ELIMINAR\|id` | `CANAL_ELIMINADO\|id` |
| `CANAL_MIEMBROS_PEDIR\|id` | `CANAL_MIEMBROS_INICIO\|id`, `CANAL_MIEMBRO\|id\|usuario`, `CANAL_MIEMBROS_FIN\|id` (solo al propietario) |
| `CUENTA_ELIMINAR\|contraseña` | `CUENTA_BORRADA\|usuario`, `CUENTA_ELIMINADA` |
| `BORRAR\|idMensaje`, `SALIR` | `DEL\|ámbito\|clave\|id`; en historiales `MD\|ámbito\|clave\|id\|hora\|autor` |
| `MARCAR_LEIDO\|ámbito\|clave\|id` | `LECTURA\|ámbito\|clave\|últimoId` (en la carga inicial del chat) |
| `FOTO_PEDIR\|usuario` | `FOTO\|usuario\|perfil` (vacío, `animal:gato` o base64), `USERS\|a,b`, `AVISO\|texto` |

## Seguridad y límites

- Las contraseñas se guardan **literalmente en la columna `Usuarios.clave`**; no se usa SHA-256, sal ni cifrado. Es una decisión para la práctica, no recomendable para cuentas reales.
- Todas las consultas SQL son preparadas (sin inyección SQL) y la base de datos hace cumplir sus propias reglas (llaves foráneas y `CHECK`).
- El servidor valida todo: solo miembros escriben en grupos, solo el dueño en su canal y solo el autor elimina un mensaje; el cliente no decide los permisos. Solo el dueño agrega, quita o elimina salas; los participantes pueden salirse de grupos ajenos. Para borrar la cuenta se comprueba de nuevo la contraseña. Las modificaciones de miembros/propietarios se autorizan en SQL y se reflejan en vivo.
- **Limitación:** no hay cifrado (TLS); el usuario y la contraseña viajan en claro por la red, y la contraseña también queda visible en SQLite. La codificación Base64 se conserva **solo para transportar fotos** en el protocolo de texto; no se usa para contraseñas ni mensajes.
- Cada mensaje admite hasta 500 bytes. El servidor atiende un hilo por cliente.

## Pruebas

`pruebas/prueba_servidor.py` levanta un servidor temporal y comprueba (62 verificaciones) registro, login, avatares, permisos, historial, eliminación de mensajes, formato de fechas, integridad de la base de datos e inyección SQL.

`pruebas/prueba_administracion.py` comprueba 45 escenarios adicionales de permisos, alta/baja de miembros, historial, salida de grupos, eliminación de salas y cuenta y cascadas de SQLite. Usa una **base de datos temporal**, nunca el archivo `chat-v2.db`:

```bash
python3 pruebas/prueba_servidor.py ./servidor        # (o ./build/servidor)
python3 pruebas/prueba_administracion.py ./servidor  # (o ./build/servidor)
```

## Licencia

MIT (ver `LICENSE`). La fuente DejaVu Sans conserva su propia licencia (`recursos/LICENSE-DejaVu.txt`).

## Ajustes de interfaz y hora (TELECHAT)

- La ventana y la pantalla de acceso muestran **TELECHAT**.
- **Administrar grupo** y **Eliminar canal** están en la esquina superior derecha del encabezado de la conversación, sin tapar la etiqueta «Mensaje». Los títulos largos se acortan con `...` cuando hace falta.
- En **Eliminar cuenta** se muestra una sola indicación de contraseña, por encima del campo correspondiente.
- La hora de cada mensaje se calcula en el **cliente** según la zona horaria del equipo: `HH:MM` para hoy y `DD/MM HH:MM` para días anteriores. Si el servidor está en UTC y el cliente en México, se verá la hora de México.
- **Hay que actualizar ambos ejecutables:** recompilar el servidor Linux con `make` y el cliente Windows con `compilar_windows.cmd`. Si se conecta el cliente nuevo a un servidor viejo, los mensajes viejos con el formato anterior seguirán apareciendo, pero no se corregirá su zona horaria hasta actualizar el servidor.
- Las fechas almacenadas anteriormente siguen siendo legibles; no se borra ni recrea `chat-v2.db`. Se supone que el servidor ejecuta con la misma zona horaria con que se guardaron esos registros.

### Pruebas de las funciones nuevas

```bash
python3 pruebas/prueba_canales_historial.py ./servidor
```

Se prueban permisos de lista de miembros, 625 mensajes de canal, constancias de borrado persistentes y recuperación del historial completo con SQLite temporal.

## Contraseñas literales y notificaciones de mensajes (última actualización)

- Se eliminaron los archivos `Sha256.cpp` y `Sha256.h`, así como su uso en la compilación y el servidor. Los registros nuevos guardan la contraseña literalmente en `Usuarios.clave`.
- **No se pierden las cuentas ni el historial** al migrar desde v2, pero las contraseñas antiguas, al estar hasheadas, deben redefinirse con `configurar_claves.py` como se explica arriba. Antes de migrar se crea un respaldo del `.db`.
- Los puntos de notificación ya **no aparecen por reconstruir el historial ni por añadir grupos a la lista**, y tus propios mensajes no activan puntos.
- Cuando se abre una conversación, el cliente comunica el último ID visto con `MARCAR_LEIDO`; el servidor lo registra en `Lecturas`. Así, al reiniciar sesión **solo se marca lo que llegó después de la última lectura**, incluidos los mensajes recibidos mientras estabas desconectado.
- La primera conexión de cada usuario tras la actualización toma su historial existente como leído (punto de partida); esto evita que todos los chats antiguos aparezcan como nuevos al actualizar.

Pruebas de estos cambios:

```bash
python3 pruebas/prueba_lecturas_claves.py ./servidor
```

`pruebas/prueba_notificaciones_cliente.cpp` comprueba que el modelo del cliente no encienda el punto para mensajes antiguos, mensajes propios o una lista de grupos cargada. Necesita compilarse con SFML; no forma parte del ejecutable de producción.
