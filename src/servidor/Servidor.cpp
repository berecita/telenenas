#include "Servidor.h"

#include "../comun/Base64.h"
#include "../comun/Protocolo.h"
#include "Sha256.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <random>
#include <set>
#include <thread>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

using namespace std;

static atomic<bool> apagando{false};
static mutex mutexLog;

void solicitarApagado() {
    apagando.store(true);
}

static void escribirLog(const string& texto) {
    lock_guard<mutex> g(mutexLog);
    cout << texto << endl;
}

// ---------------------------------------------------------------------
// Utilidades
// ---------------------------------------------------------------------

static string minusculas(string s) {
    transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return tolower(c); });
    return s;
}

static string recortar(const string& s) {
    size_t i = 0, j = s.size();
    while (i < j && isspace(static_cast<unsigned char>(s[i]))) i++;
    while (j > i && isspace(static_cast<unsigned char>(s[j - 1]))) j--;
    return s.substr(i, j - i);
}

// Corta a `maximo` bytes sin partir un caracter UTF-8 a la mitad.
static string limitarBytes(const string& s, size_t maximo) {
    if (s.size() <= maximo) return s;

    size_t corte = maximo;
    while (corte > 0 && (static_cast<unsigned char>(s[corte]) & 0xC0) == 0x80) corte--;
    return s.substr(0, corte);
}

static size_t contarCaracteres(const string& s) {
    size_t n = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) n++;
    return n;
}

// "" si el nombre sirve; si no, el motivo.
static string validarNombre(const string& nombre, size_t maxCaracteres, const string& que) {
    if (nombre.empty()) return "El " + que + " no puede estar vacio.";
    if (contarCaracteres(nombre) > maxCaracteres) {
        return "El " + que + " admite maximo " + to_string(maxCaracteres) + " caracteres.";
    }

    for (unsigned char c : nombre) {
        if (c < 32 || c == 127 || c == '|' || c == ',') {
            return "El " + que + " no puede llevar '|' ni ','.";
        }
    }

    return "";
}

// La base de datos guarda la fecha como texto "AAAA-MM-DD HH:MM:SS" (hora local).
// Al cliente le mandamos "HH:MM" si es de hoy, o "DD/MM HH:MM" si es de otro dia.
static string fechaTexto(const string& fecha) {
    if (fecha.size() < 16) return fecha;

    time_t ahora = time(nullptr);
    tm local;
    localtime_r(&ahora, &local);

    char hoy[16];
    strftime(hoy, sizeof(hoy), "%Y-%m-%d", &local);

    string hora = fecha.substr(11, 5);

    if (fecha.compare(0, 10, hoy) == 0) {
        return hora;
    }

    return fecha.substr(8, 2) + "/" + fecha.substr(5, 2) + " " + hora;
}

static string unir(const vector<string>& v, char separador = ',') {
    string r;
    for (size_t i = 0; i < v.size(); i++) {
        if (i > 0) r += separador;
        r += v[i];
    }
    return r;
}

static bool aNumero(const string& texto, long long& valor) {
    try {
        size_t usados = 0;
        valor = stoll(texto, &usados);
        return usados == texto.size();
    } catch (...) {
        return false;
    }
}

// ---- contrasenas: sal aleatoria + SHA-256 repetido ----

static string generarSal() {
    unsigned char bytes[16];
    ifstream urandom("/dev/urandom", ios::binary);

    if (urandom) {
        urandom.read(reinterpret_cast<char*>(bytes), sizeof(bytes));
    }

    if (!urandom) {
        random_device rd;
        for (auto& b : bytes) b = static_cast<unsigned char>(rd());
    }

    static const char HEX[] = "0123456789abcdef";
    string sal;
    for (unsigned char b : bytes) {
        sal += HEX[b >> 4];
        sal += HEX[b & 0xF];
    }
    return sal;
}

static string hashClave(const string& sal, const string& clave) {
    string h = sha256Hex(sal + clave);
    for (int i = 0; i < 5000; i++) {
        h = sha256Hex(h + sal + clave);
    }
    return h;
}

static bool igualesSeguro(const string& a, const string& b) {
    if (a.size() != b.size()) return false;
    unsigned char r = 0;
    for (size_t i = 0; i < a.size(); i++) r |= static_cast<unsigned char>(a[i] ^ b[i]);
    return r == 0;
}

// ---- sockets ----

static bool enviarLinea(int fd, const string& mensaje) {
    string datos = mensaje + "\n";
    size_t total = 0;

    while (total < datos.size()) {
        ssize_t n = send(fd, datos.c_str() + total, datos.size() - total, MSG_NOSIGNAL);
        if (n <= 0) return false;
        total += static_cast<size_t>(n);
    }

    return true;
}

static bool leerLinea(Sesion& s, string& linea) {
    while (true) {
        size_t pos = s.entrada.find('\n');

        if (pos != string::npos) {
            linea = s.entrada.substr(0, pos);
            s.entrada.erase(0, pos + 1);
            if (!linea.empty() && linea.back() == '\r') linea.pop_back();
            return true;
        }

        if (s.entrada.size() > MAX_LINEA) return false; // linea absurdamente larga

        char buffer[4096];
        ssize_t n = recv(s.fd, buffer, sizeof(buffer), 0);

        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;

        s.entrada.append(buffer, static_cast<size_t>(n));
    }
}

// Linea de mensaje para el cliente. `clave` es la que ve ESE cliente.
static string lineaMensaje(const MensajeGuardado& m, const string& clave) {
    return "M|" + m.ambito + "|" + clave + "|" + to_string(m.id) + "|" + fechaTexto(m.fecha) + "|" +
           m.autor + "|" + m.texto;
}

// ---------------------------------------------------------------------
// Envio
// ---------------------------------------------------------------------

Servidor::Servidor(int puerto, const string& rutaBD) : puerto(puerto), rutaBD(rutaBD) {}

bool Servidor::enviar(const SesionPtr& s, const string& mensaje) {
    if (!s->activa.load()) return false;

    lock_guard<mutex> g(s->envio);

    if (!enviarLinea(s->fd, mensaje)) {
        s->activa = false;
        shutdown(s->fd, SHUT_RDWR); // despierta al hilo que esta en recv()
        return false;
    }

    return true;
}

void Servidor::enviarA(const string& usuario, const string& mensaje) {
    SesionPtr s;

    {
        lock_guard<mutex> g(mutexEnLinea);
        auto it = enLinea.find(minusculas(usuario));
        if (it != enLinea.end()) s = it->second;
    }

    if (s) enviar(s, mensaje); // fuera del mutex: un cliente lento no bloquea a los demas
}

void Servidor::enviarAVarios(const vector<string>& usuarios, const string& mensaje) {
    for (const string& u : usuarios) enviarA(u, mensaje);
}

void Servidor::difundir(const string& mensaje) {
    vector<SesionPtr> todas;

    {
        lock_guard<mutex> g(mutexEnLinea);
        for (auto& par : enLinea) todas.push_back(par.second);
    }

    for (auto& s : todas) enviar(s, mensaje);
}

void Servidor::difundirUsuarios() {
    vector<string> nombres;

    {
        lock_guard<mutex> g(mutexEnLinea);
        for (auto& par : enLinea) nombres.push_back(par.second->usuario);
    }

    sort(nombres.begin(), nombres.end(), [](const string& a, const string& b) {
        return minusculas(a) < minusculas(b);
    });

    difundir("USERS|" + unir(nombres));
}

// ---------------------------------------------------------------------
// Conexion de un cliente
// ---------------------------------------------------------------------

// Espera REGISTRO o LOGIN. Regresa true cuando el usuario ya quedo dentro.
bool Servidor::autenticar(const SesionPtr& s) {
    string linea;

    while (!apagando.load() && leerLinea(*s, linea)) {
        vector<string> c = separarMaximo(linea, 4);
        string nombre, motivo, nombreReal;

        if (c[0] == "REGISTRO" && c.size() >= 3) {
            nombre = recortar(c[1]);
            const string& clave = c[2];
            string perfil = (c.size() >= 4) ? c[3] : "";
            string avatar, foto;

            motivo = validarNombre(nombre, MAX_NOMBRE_USUARIO, "usuario");

            if (motivo.empty() && (clave.size() < MIN_CLAVE || clave.size() > MAX_CLAVE)) {
                motivo = "La contrasena debe tener entre " + to_string(MIN_CLAVE) + " y " +
                         to_string(MAX_CLAVE) + " caracteres.";
            }

            if (motivo.empty() && clave.find_first_of(string("|\r\n")) != string::npos) {
                motivo = "La contrasena no puede llevar '|'.";
            }

            if (motivo.empty() && !perfil.empty()) {
                if (perfil.rfind("animal:", 0) == 0) {            // avatar predeterminado
                    avatar = perfil.substr(7);
                    if (!esAvatarPredeterminado(avatar)) motivo = "Ese avatar no existe.";
                } else {                                          // foto propia (base64)
                    string bytes;
                    if (perfil.size() != FOTO_B64_LONGITUD || !base64Decodificar(perfil, bytes) ||
                        bytes.size() != FOTO_BYTES) {
                        motivo = "La foto de perfil no es valida.";
                    } else {
                        avatar = "foto";
                        foto = perfil;
                    }
                }
            }

            if (motivo.empty() && !bd.nombreCanonico(nombre).empty()) {
                motivo = "Ese usuario ya existe.";
            }

            if (motivo.empty()) {
                string sal = generarSal();

                if (!bd.crearUsuario(nombre, sal, hashClave(sal, clave), avatar, foto)) {
                    motivo = "No se pudo crear la cuenta.";
                } else {
                    nombreReal = nombre;
                    escribirLog("[+] Cuenta nueva: " + nombre);
                }
            }

        } else if (c[0] == "LOGIN" && c.size() >= 3) {
            nombre = recortar(c[1]);
            string sal, hash;

            // El mismo mensaje si el usuario no existe o la clave falla: no revela cuales usuarios existen.
            if (!bd.credenciales(nombre, nombreReal, sal, hash) ||
                !igualesSeguro(hash, hashClave(sal, c[2]))) {
                motivo = "Usuario o contrasena incorrectos.";
            }

        } else {
            motivo = "Primero debes iniciar sesion o registrarte.";
        }

        if (motivo.empty()) {
            lock_guard<mutex> g(mutexEnLinea);
            string clave = minusculas(nombreReal);

            if (enLinea.count(clave)) {
                motivo = "Ese usuario ya tiene una sesion abierta.";
            } else {
                s->usuario = nombreReal;
                enLinea[clave] = s;
            }
        }

        if (!motivo.empty()) {
            enviar(s, "ERR|" + motivo);
            continue;
        }

        enviar(s, "OK|" + s->usuario);
        return true;
    }

    return false;
}

// Manda todo lo que el cliente necesita para pintar su pantalla: historiales,
// grupos y canales. (Las fotos las pide el cliente cuando las necesita.)
void Servidor::enviarEstadoInicial(const SesionPtr& s) {
    const string& yo = s->usuario;
    const int LIMITE = 50;

    for (const auto& m : bd.historial("G", "general", LIMITE)) {
        enviar(s, lineaMensaje(m, "general"));
    }

    for (const string& otro : bd.companerosDirectos(yo)) {
        for (const auto& m : bd.historialDirecto(yo, otro, LIMITE)) {
            enviar(s, lineaMensaje(m, otro));
        }
    }

    for (const auto& g : bd.gruposDe(yo)) {
        enviar(s, "GRUPO|" + to_string(g.id) + "|" + g.nombre + "|" + g.dueno + "|" +
                  unir(bd.miembrosGrupo(g.id)));

        for (const auto& m : bd.historial("R", to_string(g.id), LIMITE)) {
            enviar(s, lineaMensaje(m, to_string(g.id)));
        }
    }

    for (const auto& c : bd.todosLosCanales()) {
        bool unido = bd.esMiembroCanal(c.id, yo);
        enviar(s, "CANAL|" + to_string(c.id) + "|" + c.nombre + "|" + c.dueno + "|" + (unido ? "1" : "0"));

        if (unido) {
            for (const auto& m : bd.historial("C", to_string(c.id), LIMITE)) {
                enviar(s, lineaMensaje(m, to_string(c.id)));
            }
        }
    }
}

void Servidor::atender(SesionPtr s) {
    if (autenticar(s)) {
        escribirLog("[+] " + s->usuario + " inicio sesion.");
        enviarEstadoInicial(s);
        difundirUsuarios();

        string linea;
        bool salir = false;

        while (!salir && leerLinea(*s, linea)) {
            procesarComando(s, linea, salir);
        }

        {
            lock_guard<mutex> g(mutexEnLinea);
            auto it = enLinea.find(minusculas(s->usuario));
            if (it != enLinea.end() && it->second == s) enLinea.erase(it);
        }

        escribirLog("[-] " + s->usuario + " salio.");
    }

    s->activa = false;

    {
        lock_guard<mutex> g(s->envio); // esperar a que nadie este escribiendo antes de cerrar
        close(s->fd);
        s->fd = -1;
    }

    if (!s->usuario.empty() && !apagando.load()) {
        difundirUsuarios();
    }
}

void Servidor::procesarComando(const SesionPtr& s, const string& linea, bool& salir) {
    vector<string> c = separarMaximo(linea, 3);
    const string& cmd = c[0];

    // Comandos con 1 argumento (el texto es TODO lo que sigue, aunque lleve '|').
    if (cmd == "GENERAL") {
        vector<string> d = separarMaximo(linea, 2);
        if (d.size() >= 2) cmdGeneral(s, d[1]);
    } else if (cmd == "CANAL_NUEVO") {
        vector<string> d = separarMaximo(linea, 2);
        if (d.size() >= 2) cmdCanalNuevo(s, d[1]);
    } else if (cmd == "CANAL_UNIR") {
        vector<string> d = separarMaximo(linea, 2);
        if (d.size() >= 2) cmdCanalUnir(s, d[1]);
    } else if (cmd == "BORRAR") {
        vector<string> d = separarMaximo(linea, 2);
        if (d.size() >= 2) cmdBorrar(s, d[1]);
    } else if (cmd == "FOTO_PEDIR") {
        vector<string> d = separarMaximo(linea, 2);
        if (d.size() >= 2) cmdFoto(s, d[1]);

    // Comandos con 2 argumentos.
    } else if (cmd == "DM" && c.size() >= 3) {
        cmdDirecto(s, c[1], c[2]);
    } else if (cmd == "GRUPO_NUEVO" && c.size() >= 3) {
        cmdGrupoNuevo(s, c[1], c[2]);
    } else if (cmd == "GMSG" && c.size() >= 3) {
        cmdGrupoMensaje(s, c[1], c[2]);
    } else if (cmd == "CMSG" && c.size() >= 3) {
        cmdCanalMensaje(s, c[1], c[2]);

    } else if (cmd == "SALIR") {
        salir = true;
    }
    // cualquier otra cosa se ignora
}

// ---------------------------------------------------------------------
// Comandos
// ---------------------------------------------------------------------

void Servidor::cmdGeneral(const SesionPtr& s, const string& textoCrudo) {
    string texto = limitarBytes(recortar(textoCrudo), MAX_TEXTO);
    if (texto.empty()) return;

    MensajeGuardado m;
    m.ambito = "G";
    m.clave = "general";
    m.autor = s->usuario;
    m.texto = texto;

    if (!bd.guardarMensaje(m)) {
        enviar(s, "AVISO|No se pudo guardar el mensaje.");
        return;
    }

    escribirLog("[general] " + m.autor + ": " + texto);
    difundir(lineaMensaje(m, "general"));
}

void Servidor::cmdDirecto(const SesionPtr& s, const string& destinoCrudo, const string& textoCrudo) {
    string texto = limitarBytes(recortar(textoCrudo), MAX_TEXTO);
    if (texto.empty()) return;

    string destino = bd.nombreCanonico(recortar(destinoCrudo));

    if (destino.empty()) {
        enviar(s, "AVISO|El usuario '" + recortar(destinoCrudo) + "' no existe.");
        return;
    }

    if (minusculas(destino) == minusculas(s->usuario)) {
        enviar(s, "AVISO|No puedes enviarte mensajes a ti mismo.");
        return;
    }

    MensajeGuardado m;
    m.ambito = "D";
    m.autor = s->usuario;
    m.destino = destino;
    m.texto = texto;

    if (!bd.guardarMensaje(m)) {
        enviar(s, "AVISO|No se pudo guardar el mensaje.");
        return;
    }

    // No se escribe el contenido en el log: es privado.
    enviar(s, lineaMensaje(m, destino));      // yo lo veo en la conversacion con `destino`
    enviarA(destino, lineaMensaje(m, m.autor)); // el otro lo ve en la conversacion conmigo
}

void Servidor::cmdGrupoNuevo(const SesionPtr& s, const string& nombreCrudo, const string& miembrosCrudo) {
    string nombre = recortar(nombreCrudo);
    string motivo = validarNombre(nombre, MAX_NOMBRE_SALA, "nombre del grupo");

    if (!motivo.empty()) {
        enviar(s, "AVISO|" + motivo);
        return;
    }

    vector<string> miembros = {s->usuario};
    set<string> vistos = {minusculas(s->usuario)};
    size_t inicio = 0;

    while (inicio <= miembrosCrudo.size()) {
        size_t coma = miembrosCrudo.find(',', inicio);
        if (coma == string::npos) coma = miembrosCrudo.size();

        string real = bd.nombreCanonico(recortar(miembrosCrudo.substr(inicio, coma - inicio)));

        if (!real.empty() && vistos.insert(minusculas(real)).second) {
            miembros.push_back(real);
        }

        inicio = coma + 1;
    }

    if (miembros.size() < 2) {
        enviar(s, "AVISO|Elige al menos un usuario para el grupo.");
        return;
    }

    long long id = bd.crearGrupo(nombre, s->usuario, miembros);

    if (id < 0) {
        enviar(s, "AVISO|No se pudo crear el grupo.");
        return;
    }

    escribirLog("[grupo] " + s->usuario + " creo '" + nombre + "' (" + to_string(miembros.size()) + " miembros)");
    enviarAVarios(miembros, "GRUPO|" + to_string(id) + "|" + nombre + "|" + s->usuario + "|" + unir(miembros));
}

void Servidor::cmdGrupoMensaje(const SesionPtr& s, const string& idTexto, const string& textoCrudo) {
    string texto = limitarBytes(recortar(textoCrudo), MAX_TEXTO);
    long long id;

    if (texto.empty() || !aNumero(idTexto, id)) return;

    if (!bd.esMiembroGrupo(id, s->usuario)) {
        enviar(s, "AVISO|No perteneces a ese grupo.");
        return;
    }

    MensajeGuardado m;
    m.ambito = "R";
    m.clave = to_string(id);
    m.autor = s->usuario;
    m.texto = texto;

    if (!bd.guardarMensaje(m)) {
        enviar(s, "AVISO|No se pudo guardar el mensaje.");
        return;
    }

    enviarAVarios(bd.miembrosGrupo(id), lineaMensaje(m, m.clave));
}

void Servidor::cmdCanalNuevo(const SesionPtr& s, const string& nombreCrudo) {
    string nombre = recortar(nombreCrudo);
    string motivo = validarNombre(nombre, MAX_NOMBRE_SALA, "nombre del canal");

    if (!motivo.empty()) {
        enviar(s, "AVISO|" + motivo);
        return;
    }

    long long id = bd.crearCanal(nombre, s->usuario);

    if (id < 0) {
        enviar(s, "AVISO|Ya existe un canal con ese nombre.");
        return;
    }

    escribirLog("[canal] " + s->usuario + " creo '" + nombre + "'");

    // Todos los conectados ven el canal nuevo en su lista; solo el dueño aparece ya unido.
    vector<SesionPtr> todas;
    {
        lock_guard<mutex> g(mutexEnLinea);
        for (auto& par : enLinea) todas.push_back(par.second);
    }

    for (auto& otra : todas) {
        bool esDueno = (otra == s);
        enviar(otra, "CANAL|" + to_string(id) + "|" + nombre + "|" + s->usuario + "|" + (esDueno ? "1" : "0"));
    }
}

void Servidor::cmdCanalUnir(const SesionPtr& s, const string& idTexto) {
    long long id;
    SalaGuardada canal;

    if (!aNumero(idTexto, id) || !bd.canal(id, canal)) {
        enviar(s, "AVISO|Ese canal no existe.");
        return;
    }

    if (bd.esMiembroCanal(id, s->usuario)) {
        return; // ya estaba unido
    }

    bd.unirCanal(id, s->usuario);
    enviar(s, "CANAL|" + to_string(id) + "|" + canal.nombre + "|" + canal.dueno + "|1");

    // Al unirse ve todo lo que se envio antes al canal.
    for (const auto& m : bd.historial("C", to_string(id), 50)) {
        enviar(s, lineaMensaje(m, to_string(id)));
    }
}

void Servidor::cmdCanalMensaje(const SesionPtr& s, const string& idTexto, const string& textoCrudo) {
    string texto = limitarBytes(recortar(textoCrudo), MAX_TEXTO);
    long long id;
    SalaGuardada canal;

    if (texto.empty()) return;

    if (!aNumero(idTexto, id) || !bd.canal(id, canal)) {
        enviar(s, "AVISO|Ese canal no existe.");
        return;
    }

    // Regla de la practica: los que se unen solo leen; escribe unicamente el dueño.
    if (minusculas(canal.dueno) != minusculas(s->usuario)) {
        enviar(s, "AVISO|Solo el dueno del canal puede escribir en el.");
        return;
    }

    MensajeGuardado m;
    m.ambito = "C";
    m.clave = to_string(id);
    m.autor = s->usuario;
    m.texto = texto;

    if (!bd.guardarMensaje(m)) {
        enviar(s, "AVISO|No se pudo guardar el mensaje.");
        return;
    }

    enviarAVarios(bd.miembrosCanal(id), lineaMensaje(m, m.clave));
}

void Servidor::cmdFoto(const SesionPtr& s, const string& usuario) {
    string real = bd.nombreCanonico(recortar(usuario));
    if (real.empty()) return;

    string avatar, foto;
    bd.perfil(real, avatar, foto);

    // perfil: "" (sin foto), "animal:gato" o la foto propia en base64
    string perfil;
    if (avatar == "foto") perfil = foto;
    else if (!avatar.empty()) perfil = "animal:" + avatar;

    enviar(s, "FOTO|" + real + "|" + perfil);
}

// Elimina un mensaje PROPIO para todos los que lo veian.
void Servidor::cmdBorrar(const SesionPtr& s, const string& idTexto) {
    long long id;
    MensajeGuardado m;

    if (!aNumero(idTexto, id) || !bd.mensaje(id, m)) {
        enviar(s, "AVISO|Ese mensaje ya no existe.");
        return;
    }

    if (minusculas(m.autor) != minusculas(s->usuario)) {
        enviar(s, "AVISO|Solo puedes eliminar tus propios mensajes.");
        return;
    }

    if (!bd.borrarMensaje(id)) {
        enviar(s, "AVISO|No se pudo eliminar el mensaje.");
        return;
    }

    const string idTxt = to_string(id);

    if (m.ambito == "G") {
        difundir("DEL|G|general|" + idTxt);
    } else if (m.ambito == "D") {
        // cada quien lo ve bajo el nombre del OTRO
        enviarA(m.autor, "DEL|D|" + m.destino + "|" + idTxt);
        enviarA(m.destino, "DEL|D|" + m.autor + "|" + idTxt);
    } else if (m.ambito == "R") {
        enviarAVarios(bd.miembrosGrupo(stoll(m.clave)), "DEL|R|" + m.clave + "|" + idTxt);
    } else if (m.ambito == "C") {
        enviarAVarios(bd.miembrosCanal(stoll(m.clave)), "DEL|C|" + m.clave + "|" + idTxt);
    }
}

// ---------------------------------------------------------------------
// Arranque
// ---------------------------------------------------------------------

bool Servidor::iniciar() {
    if (!bd.abrir(rutaBD)) {
        return false;
    }

    int escucha = socket(AF_INET, SOCK_STREAM, 0);

    if (escucha == -1) {
        cerr << "Error creando el socket del servidor." << endl;
        return false;
    }

    int opt = 1;
    setsockopt(escucha, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in direccion;
    memset(&direccion, 0, sizeof(direccion));
    direccion.sin_family = AF_INET;
    direccion.sin_addr.s_addr = INADDR_ANY;
    direccion.sin_port = htons(static_cast<uint16_t>(puerto));

    if (bind(escucha, reinterpret_cast<sockaddr*>(&direccion), sizeof(direccion)) == -1) {
        cerr << "Error en bind() del puerto " << puerto
             << ": ya esta en uso o no tienes permiso. Usa el puerto que te asignaron." << endl;
        close(escucha);
        return false;
    }

    if (listen(escucha, 16) == -1) {
        cerr << "Error en listen()." << endl;
        close(escucha);
        return false;
    }

    cout << "Servidor de chat escuchando en el puerto " << puerto
         << " (base de datos: " << rutaBD << "). Ctrl+C para detener." << endl;

    // poll() con tiempo limite: asi se revisa la bandera de apagado cada medio segundo.
    while (!apagando.load()) {
        pollfd p;
        p.fd = escucha;
        p.events = POLLIN;
        p.revents = 0;

        if (poll(&p, 1, 500) <= 0) continue;

        sockaddr_in direccionCliente;
        socklen_t tam = sizeof(direccionCliente);
        int fd = accept(escucha, reinterpret_cast<sockaddr*>(&direccionCliente), &tam);

        if (fd == -1) continue;

        // Si un cliente deja de leer, send() no se queda bloqueado para siempre.
        timeval limite;
        limite.tv_sec = 5;
        limite.tv_usec = 0;
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &limite, sizeof(limite));

        auto sesion = make_shared<Sesion>();
        sesion->fd = fd;

        thread(&Servidor::atender, this, sesion).detach(); // un hilo por cliente
    }

    cout << "\nApagando servidor..." << endl;

    {
        lock_guard<mutex> g(mutexEnLinea);
        for (auto& par : enLinea) {
            shutdown(par.second->fd, SHUT_RDWR); // sus hilos salen de recv() y se limpian solos
        }
    }

    for (int i = 0; i < 30; i++) { // esperar (max 3 s) a que se vacie
        {
            lock_guard<mutex> g(mutexEnLinea);
            if (enLinea.empty()) break;
        }
        this_thread::sleep_for(chrono::milliseconds(100));
    }

    close(escucha);
    cout << "Servidor detenido." << endl;
    return true;
}
