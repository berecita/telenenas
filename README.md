# Chat con sockets (C++) 

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
| **Grupos** | Creas un grupo con nombre y eliges a los miembros conectados; sus mensajes solo los ven ellos. |
| **Eliminar** | Puedes eliminar tus propios mensajes en cualquier conversación (se borran para todos y de la base de datos). |
| **Canales** | Creas un canal de difusión (queda registrado en el servidor). Cualquiera puede ver la lista y unirse: al unirse ve **todo el historial** pero **solo el dueño puede escribir**. |

El historial de todas las conversaciones se guarda en la base de datos y se recupera al iniciar sesión.

## Estructura

```
src/comun/      Protocolo.h (formato de mensajes) y Base64
src/servidor/   mainServidor, Servidor (lógica), BaseDatos (SQLite), Sha256
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
| `Usuarios` | `idUsuario`, `nombre VARCHAR(16)`, `sal CHAR(32)`, `hashClave CHAR(64)`, `avatar`, `foto`, `fechaRegistro DATETIME` |
| `Mensajes` | `idMensaje`, `tipo` (G/D/R/C), `idAutor`, `idDestino`, `idGrupo`, `idCanal`, `texto VARCHAR(500)`, `fechaEnvio DATETIME` |
| `Grupos` / `GrupoMiembros` | `idGrupo`, `nombre`, `idDueno`, `fechaCreacion` / `idGrupo`, `idUsuario`, `fechaUnion` |
| `Canales` / `CanalMiembros` | `idCanal`, `nombre` (único), `idDueno`, `fechaCreacion` / `idCanal`, `idUsuario`, `fechaUnion` |

Las fechas son `DATETIME` en texto `AAAA-MM-DD HH:MM:SS` (hora local del servidor), así que se leen directo, sin funciones de conversión:

```bash
sqlite3 -header -column chat.db "SELECT m.idMensaje, u.nombre AS autor, m.texto, m.fechaEnvio FROM Mensajes m JOIN Usuarios u ON u.idUsuario = m.idAutor;"
```

> Si tienes un `chat.db` creado con una versión anterior del proyecto, el servidor se niega a usarlo y lo dice: bórralo (`rm chat.db`) y se crea uno nuevo.

## Compilar y ejecutar el cliente

Requisitos: CMake ≥ 3.16 y un compilador C++17. Si no hay **SFML 2.5/2.6** instalada, CMake la descarga y compila sola (necesita internet la primera vez y `git`).

```bash
cmake -S . -B build -DBUILD_SERVIDOR=OFF
cmake --build build --config Release
```

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
5. **Eliminar un mensaje tuyo:** pasa el mouse sobre su nombre y pulsa «eliminar»; pide confirmación con un segundo clic («¿seguro?»).

## Protocolo

Un mensaje es una línea de texto terminada en `\n`, con campos separados por `|` (el último campo puede llevar `|`). La referencia completa está en `src/comun/Protocolo.h`.

| Cliente → servidor | Servidor → cliente |
|---|---|
| `REGISTRO\|usuario\|clave\|foto` / `LOGIN\|usuario\|clave` | `OK\|usuario` / `ERR\|motivo` |
| `GENERAL\|texto`, `DM\|destino\|texto` | `M\|ámbito\|clave\|id\|hora\|autor\|texto` |
| `GRUPO_NUEVO\|nombre\|m1,m2`, `GMSG\|id\|texto` | `GRUPO\|id\|nombre\|dueño\|miembros` |
| `CANAL_NUEVO\|nombre`, `CANAL_UNIR\|id`, `CMSG\|id\|texto` | `CANAL\|id\|nombre\|dueño\|unido` |
| `BORRAR\|idMensaje`, `SALIR` | `DEL\|ámbito\|clave\|id` |
| `FOTO_PEDIR\|usuario` | `FOTO\|usuario\|perfil` (vacío, `animal:gato` o base64), `USERS\|a,b`, `AVISO\|texto` |

## Seguridad y límites

- Las contraseñas se guardan **con sal y SHA-256 repetido 5000 veces**, nunca en claro.
- Todas las consultas SQL son preparadas (sin inyección SQL) y la base de datos hace cumplir sus propias reglas (llaves foráneas y `CHECK`).
- El servidor valida todo: solo miembros escriben en grupos, solo el dueño en su canal y solo el autor elimina un mensaje; el cliente no decide los permisos.
- **Limitación:** no hay cifrado (TLS); usuario y contraseña viajan en claro por la red. Es una práctica de sockets, no un sistema para producción.
- Cada mensaje admite hasta 500 bytes. El servidor atiende un hilo por cliente.

## Pruebas

`pruebas/prueba_servidor.py` levanta un servidor temporal y comprueba (61 verificaciones) registro, login, avatares, permisos, historial, eliminación de mensajes, formato de fechas, integridad de la base de datos e inyección SQL:

```bash
python3 pruebas/prueba_servidor.py ./servidor        # (o ./build/servidor)
```

## Licencia

MIT (ver `LICENSE`). La fuente DejaVu Sans conserva su propia licencia (`recursos/LICENSE-DejaVu.txt`).

## Ejecutables para Linux y macOS

En GitHub, abre **Actions → Clientes Linux y macOS → Run workflow**. Al
terminar, descarga el paquete correspondiente desde **Artifacts**.

| Paquete | Equipo |
|---|---|
| `telenenas-linux-x64` | Ubuntu 22.04 o distribución compatible en x86-64 con sesión gráfica |
| `telenenas-macos-arm64` | Mac con Apple Silicon, macOS 12 o posterior |
| `telenenas-macos-x64` | Mac Intel, macOS 12 o posterior |

Extrae el ZIP de GitHub y luego el archivo `.tar.gz` que contiene. Desde la
terminal, entra en la carpeta extraída y ejecuta:

```sh
./Abrir-Telenenas
```

El lanzador usa el servidor de la práctica en `3.149.102.2:3041`. Si necesitan
otra dirección, ejecuten `./Abrir-Telenenas HOST PUERTO`.

En Linux, el binario usa las bibliotecas gráficas del sistema. Si faltan en
Ubuntu 22.04, instala `libx11-6 libxrandr2 libxcursor1 libudev1 libfreetype6 libgl1`.
Los paquetes de macOS incluyen FreeType enlazado estáticamente y llevan firma
ad hoc para la prueba. Si macOS bloquea el binario descargado, un paquete para
distribución pública sin ese aviso requiere firma con Developer ID y notarización.

Para que los ejecutables estén disponibles directamente después de clonar,
extrae los paquetes verificados en `dist/` y sube esas carpetas al repositorio.
Cada carpeta debe conservar el ejecutable, el lanzador y los recursos juntos.
