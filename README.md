# Telenenas · TELECHAT

TELECHAT es una aplicación de mensajería de escritorio desarrollada en C++17 para la práctica de Sistemas Distribuidos. Varias personas pueden iniciar sesión desde Windows, GNU/Linux o macOS, intercambiar mensajes y recuperar sus conversaciones al volver a entrar. Para probarla se necesita **un servidor de chat en el equipo Linux de la clase** y **al menos dos clientes con cuentas distintas**.

## Cómo se realizó

| Componente | Implementación y función |
|---|---|
| Cliente gráfico | SFML 2.x dibuja las pantallas de acceso y conversación. El cliente envía las acciones del usuario por un socket TCP y actualiza la ventana al recibir mensajes, cambios de miembros o avisos del servidor. Código en [src/cliente](src/cliente). |
| Servidor de chat | Un programa de consola abre el puerto TCP asignado, acepta conexiones y atiende cada cliente en un hilo. Valida las credenciales, comprueba quién puede publicar o eliminar y distribuye cada mensaje a sus destinatarios. Código en [src/servidor](src/servidor). |
| Protocolo | Cliente y servidor usan líneas de texto con campos separados por barras verticales. Por ejemplo, al enviar un mensaje general, el servidor guarda el registro y devuelve a los clientes su identificador, autor, hora y contenido. Definición en [Protocolo.h](src/comun/Protocolo.h). |
| Base de datos | El servidor usa consultas con parámetros mediante ODBC Driver 18 para Microsoft SQL Server. Las ocho tablas guardan usuarios, grupos, canales, membresías, mensajes, constancias de eliminación y lecturas. Definición en [sql/esquema.sql](sql/esquema.sql). |

### Qué permite hacer

- **Cuenta y perfil.** Registrarse, iniciar sesión, elegir entre cinco avatares o cargar una imagen propia. La foto se ajusta a 64 × 64 píxeles. El usuario puede eliminar su cuenta tras confirmar su contraseña.
- **General y directos.** General muestra a los conectados el mensaje y su autor. Directos permite conversar en privado con otro usuario.
- **Grupos.** Crear un grupo con nombre y miembros, conversar dentro del grupo y abandonarlo. El propietario puede agregar o quitar integrantes y eliminar el grupo.
- **Canales.** Crear un canal de difusión o unirse a uno existente. Solo el propietario publica. Los suscriptores pueden consultar todo el historial, incluso los mensajes anteriores a su incorporación.
- **Historial.** El cliente muestra la hora local y marca conversaciones con novedades. Al eliminar un mensaje propio queda una constancia sin el texto original. General, directos y grupos cargan inicialmente hasta 50 mensajes por conversación.

## 1. Iniciar el servidor del equipo

Desde Windows, Linux o macOS se puede administrar el servidor por SSH. El comando escrito en esa sesión se ejecuta en el **equipo Linux remoto**. Windows dispone de un ejecutable de cliente. El servidor usa sockets POSIX y el proyecto no incluye un servidor nativo de Windows.

**Desde la computadora del responsable**, abre una terminal y entra con la llave y la cuenta Linux asignadas. Sustituye los dos valores de ejemplo:

```bash
ssh -i "RUTA_A_LA_LLAVE.pem" USUARIO_LINUX@3.149.102.2
```

**Dentro de SSH**, clona el repositorio si es la primera vez:

```bash
git clone https://github.com/berecita/telenenas.git
cd telenenas
```

Si ya existe el clon, entra con `cd ~/telenenas`. Para actualizar una copia en la rama `main`, usa `git pull --ff-only origin main`.

El equipo Linux necesita GNU Make, g++, los encabezados de unixODBC (`unixodbc-dev` en Debian/Ubuntu), ODBC Driver 18 y el servicio de SQL Server. En la instalación de clase ya existen la base `Telenenas`, la cuenta SQL `telenenas` y sus ocho tablas. Para preparar las tablas en otra instalación de esa base, el responsable puede ejecutar desde la raíz del clon:

```bash
/opt/mssql-tools18/bin/sqlcmd -S localhost -U administrator -d Telenenas -C -b -i sql/esquema.sql
```

Compila el servidor y captura la contraseña de la **cuenta SQL del equipo**. Ejecuta las líneas una a una. Después de `read`, escribe la contraseña y pulsa Enter.

```bash
make
read -rsp "Contraseña SQL de telenenas: " TELENENAS_DB_PASSWORD
echo
TELENENAS_DB_PASSWORD="$TELENENAS_DB_PASSWORD" ./servidor 3041 Telenenas
```

El mensaje esperado es **Servidor de chat escuchando en el puerto 3041 (base de datos: Telenenas)**. Mantén abierta esa sesión durante las pruebas. Al terminar, pulsa Ctrl+C y ejecuta `unset TELENENAS_DB_PASSWORD`.

## 2. Abrir un cliente en cada computadora

**En la terminal local de cada participante**, clona el repositorio y entra a la carpeta del proyecto. Si ya tienes la copia, abre la terminal en la carpeta que contiene este README.

```bash
git clone https://github.com/berecita/telenenas.git
cd telenenas
```

Elige el bloque correspondiente. Los comandos entran en la carpeta del paquete para que el programa encuentre la fuente y los avatares en `recursos`. La IP y el puerto corresponden al **servidor del chat**. La contraseña SQL solo se usa en el equipo servidor.

**Windows · CMD**

```bat
cd dist\windows
cliente.exe 3.149.102.2 3041
```

Desde el Explorador también se puede abrir `dist\windows\Abrir-Telenenas.cmd` con doble clic.

**GNU/Linux · Intel o AMD de 64 bits**

```bash
cd dist/telenenas-linux-x64
./cliente 3.149.102.2 3041
```

**macOS · Apple Silicon**

```bash
cd dist/telenenas-macos-arm64
./cliente 3.149.102.2 3041
```

**macOS · Intel**

```bash
cd dist/telenenas-macos-x64
./cliente 3.149.102.2 3041
```

En «Acerca de esta Mac» puedes identificar el chip. Los dos binarios macOS declaran macOS 12.0 como versión mínima. En Linux y macOS también puedes ejecutar `./Abrir-Telenenas` dentro de la carpeta seleccionada. El lanzador ya incluye la IP y el puerto.

### Compilación opcional del cliente

Los paquetes de `dist/` permiten comenzar la prueba directamente. Para compilar desde el código fuente se necesita Git, un compilador C++17, CMake 3.16 o posterior y SFML 2.5/2.6. Si no encuentra SFML 2.x, CMake intenta descargar SFML 2.6.1 y necesita acceso a Internet y las dependencias gráficas del sistema.

- **Windows:** con Visual Studio y la carga de trabajo de C++ de escritorio, abre CMD en la raíz del repositorio y ejecuta `compilar_windows.cmd`. Con el generador de Visual Studio, el resultado habitual es `build-windows\Release\cliente.exe`.
- **GNU/Linux:** instala el compilador, CMake y los paquetes de desarrollo de SFML 2.x de tu distribución. Para compilar SFML desde sus fuentes, el flujo del proyecto detalla las dependencias.
- **macOS:** usa las herramientas de línea de comandos de Xcode y CMake. Selecciona un entorno de compilación compatible con la arquitectura del equipo.

Para Linux o macOS, desde la raíz:

```bash
cmake -S . -B build-cliente -DBUILD_SERVIDOR=OFF
cmake --build build-cliente --config Release
cd build-cliente
./cliente 3.149.102.2 3041
```

Con un generador multiconfiguración, busca el ejecutable en la subcarpeta `Release`. Las opciones de compilación y empaquetado utilizadas por el proyecto están en [CMakeLists.txt](CMakeLists.txt) y en [el flujo de clientes de escritorio](.github/workflows/clientes-escritorio.yml).

## 3. Probar el intercambio entre sistemas

Deja el servidor activo durante toda la prueba. Abre dos clientes con cuentas distintas y agrega un tercero para comprobar privacidad. Para una demostración con las tres plataformas, usa una cuenta en Windows, otra en Linux y otra en macOS. En la pantalla inicial verifica **Servidor `3.149.102.2`** y **Puerto `3041`**. Elige **Registrarme** la primera vez y **Entrar** cuando ya exista la cuenta. Usa contraseñas exclusivas para la práctica: el protocolo de chat usa TCP sin TLS y la implementación guarda la clave del chat como texto en SQL Server.

| Prueba | Acción | Resultado esperado |
|---|---|---|
| Registro y perfil | Elige un avatar o indica la ruta de una imagen al registrar la cuenta. | El cliente abre el chat y muestra el perfil. |
| General | Envía un texto identificable desde cada sistema, por ejemplo `Prueba desde Mac`. | Los demás clientes reciben el mensaje con el nombre del autor. |
| Directos | Desde A selecciona a B y envíale un mensaje. Mantén abierta una tercera cuenta C. | A y B ven la conversación. C no recibe ese mensaje. |
| Grupos | A crea un grupo con B. B escribe dentro del grupo. | A recibe el mensaje en ese grupo. C queda fuera de la conversación. |
| Canales | A crea un canal y publica antes de que B se una. Después B se une y A publica de nuevo. | B puede leer la publicación anterior y recibir la nueva. Solo A puede escribir. |
| Persistencia y eliminación | Cierra y vuelve a abrir el cliente. Revisa el historial y elimina un mensaje propio de prueba. | El historial vuelve a aparecer. Los participantes ven la constancia de eliminación. |

## 4. Verificar en SQL Server

Abre **otra sesión SSH** y entra a la base del equipo. Escribe la contraseña cuando la solicite:

```bash
/opt/mssql-tools18/bin/sqlcmd -S localhost -U telenenas -d Telenenas -C -W
```

Al ver `1>`, pega esta consulta y termina con `GO` en su propia línea:

```sql
SELECT TOP (10) u.nombre AS autor, m.texto, m.fechaEnvio
FROM dbo.Mensajes AS m
JOIN dbo.Usuarios AS u ON u.idUsuario = m.idAutor
WHERE m.tipo = 'G'
ORDER BY m.idMensaje DESC;
GO
```

Busca el texto que enviaste y su autor. Ver el mismo mensaje en el cliente receptor y en la consulta verifica el recorrido completo. Escribe `EXIT` para volver a la terminal.

## Si algo falla

| Síntoma | Qué revisar |
|---|---|
| El cliente abre con `127.0.0.1` o `localhost` | El ejecutable sin argumentos propone la máquina local y el puerto 5000. Escribe `3.149.102.2` y `3041` en el formulario o usa el lanzador. En la consulta SQL por SSH, `localhost` sí es correcto. |
| La ventana abre pero no conecta | Confirma el mensaje de escucha en SSH y que ambos clientes usan la misma IP y puerto. Desde macOS/Linux puedes comprobar el acceso TCP con `nc -vz 3.149.102.2 3041`. |
| macOS no llega a mostrar la ventana | Comprueba el chip, la versión de macOS y la carpeta elegida. Ejecuta desde Terminal para ver el error exacto de arranque. |
| Faltan avatares o fuentes | Conserva la carpeta `recursos` junto al ejecutable e inicia el cliente desde la carpeta del paquete. |
| El servidor falla antes de escuchar | Revisa el controlador ODBC 18, la contraseña de la cuenta SQL y el acceso a las ocho tablas. Si el error menciona `bind`, comprueba si ya está corriendo otra instancia del chat en el puerto 3041. |

## Licencia

[Licencia MIT del proyecto](dist/telenenas-linux-x64/LICENSE-Telenenas.txt). Los paquetes incluyen también los avisos de licencia de sus recursos y dependencias.
