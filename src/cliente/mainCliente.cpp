// =====================================================================
//  TELECHAT - Cliente grafico (SFML)
//
//  Funciona en Windows, Linux y macOS: la red usa sf::TcpSocket (SFML
//  Network) y NO hay hilos. Un solo ciclo hace, en cada cuadro:
//      1) eventos del teclado/mouse
//      2) conexion.actualizar()  -> manda y recibe lo pendiente (no bloquea)
//      3) dibujar
//
//  Pantallas:  LOGIN (servidor, usuario, contrasena, foto)  ->  CHAT
//  Secciones:  General | Directos | Grupos | Canales
//
//  Uso:  cliente [ip] [puerto]      (tambien se editan en la pantalla de login)
// =====================================================================

#include "../comun/Protocolo.h"
#include "Avatares.h"
#include "Chat.h"
#include "Conexion.h"
#include "Ventana.h"
#include "Widgets.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace std;

enum class Pantalla { LOGIN, CHAT };
enum class Fase { EDITANDO, CONECTANDO, ESPERANDO };
enum class Seccion { GENERAL, DIRECTOS, GRUPOS, CANALES };
enum class Dialogo { NINGUNO, ADMIN_GRUPO, MIEMBROS_CANAL, CONFIRMAR, CUENTA };
enum class AccionConfirmar { NINGUNA, QUITAR_MIEMBRO, SALIR_GRUPO, ELIMINAR_GRUPO, ELIMINAR_CANAL };

// ---- Paleta rosa (basada en #FF99DC) ----
static const sf::Color ROSA(255, 153, 220);          // #FF99DC (color principal)
static const sf::Color FONDO(255, 244, 251);
static const sf::Color BARRA_LATERAL = ROSA;
static const sf::Color PANEL_LISTA(255, 225, 244);
static const sf::Color TEXTO_OSCURO(74, 30, 64);     // ciruela
static const sf::Color TEXTO_SUAVE(150, 100, 138);
static const sf::Color ROJO_ERROR(200, 40, 90);
static const sf::Color VERDE(30, 140, 90);           // "en linea"
static const sf::Color SELECCION(255, 190, 232);
static const sf::Color ACENTO(214, 60, 150);         // puntito de "mensajes nuevos"
static const sf::Color BORDE(238, 190, 222);
static const sf::Color BOTON_LATERAL(255, 232, 247); // botones sobre la barra rosa

// ---- Medidas ----
static const int ANCHO = 1000;
static const int ALTO = 640;
static const float LISTA_X = 200.0f;
static const float LISTA_ANCHO = 210.0f;
static const float LISTA_Y = 70.0f;
static const float FILA_ALTO = 44.0f;

// Selector de avatares de la pantalla de registro
static const float AVATAR_X = 350.0f;
static const float AVATAR_Y = 394.0f;
static const float AVATAR_D = 48.0f;
static const float AVATAR_PASO = 63.0f;

// Control "eliminar" que aparece en el encabezado de MIS mensajes
static const float BORRAR_X = AREA_X + AREA_ANCHO - 84.0f;
static const float BORRAR_ANCHO = 70.0f;

struct Intento {
    bool registro = false;
    string ip;
    int puerto = 0;
    string usuario;
    string clave;
    string foto; // base64 (solo al registrarse)
};

struct ItemLista {
    string clave;
    string titulo;
    string subtitulo;
    string avatar;       // si no esta vacio se dibuja la foto de ese usuario
    bool enLinea = false;
    bool nuevos = false;
    bool conCheck = false;
    bool marcado = false;
    bool clicable = true;
};

// ---------------------------------------------------------------------
// Utilidades
// ---------------------------------------------------------------------
static string quitarEspacios(const string& s) {
    size_t i = 0, j = s.size();
    while (i < j && isspace(static_cast<unsigned char>(s[i]))) i++;
    while (j > i && isspace(static_cast<unsigned char>(s[j - 1]))) j--;
    return s.substr(i, j - i);
}

static void enfocar(vector<CampoTexto*>& campos, CampoTexto* objetivo) {
    for (CampoTexto* c : campos) c->setEnfocado(c == objetivo);
}

static void siguienteCampo(vector<CampoTexto*>& campos) {
    for (size_t i = 0; i < campos.size(); i++) {
        if (campos[i]->estaEnfocado()) {
            enfocar(campos, campos[(i + 1) % campos.size()]);
            return;
        }
    }

    if (!campos.empty()) enfocar(campos, campos[0]);
}

static void clicEnCampos(vector<CampoTexto*>& campos, float mx, float my) {
    for (CampoTexto* c : campos) {
        if (c->contiene(mx, my)) {
            enfocar(campos, c);
            return;
        }
    }
}

static void teclearEnCampos(vector<CampoTexto*>& campos, sf::Uint32 unicode) {
    for (CampoTexto* c : campos) c->recibirCaracter(unicode);
}

// Ctrl+V (o Cmd+V en Mac): pega el portapapeles en el campo que tenga el foco.
static void pegarEnCampos(vector<CampoTexto*>& campos) {
    sf::String texto = sf::Clipboard::getString();

    for (sf::Uint32 caracter : texto) {
        if (caracter >= 32) {
            teclearEnCampos(campos, caracter);
        }
    }
}

static void rectangulo(Ventana& v, float x, float y, float ancho, float alto, sf::Color relleno) {
    sf::RectangleShape r(sf::Vector2f(ancho, alto));
    r.setPosition(x, y);
    r.setFillColor(relleno);
    v.getWindow().draw(r);
}

// Acorta texto UTF-8 sin cortar acentos; deja libre la columna de botones.
static string acortarTexto(Ventana& ventana, string texto, unsigned int tamano, float anchoMax) {
    if (ventana.medirTexto(texto, tamano) <= anchoMax) return texto;
    const string puntos = "...";
    while (!texto.empty() && ventana.medirTexto(texto + puntos, tamano) > anchoMax) {
        size_t pos = texto.size() - 1;
        while (pos > 0 && (static_cast<unsigned char>(texto[pos]) & 0xC0) == 0x80) --pos;
        texto.resize(pos);
    }
    return texto + puntos;
}

// Lo que se muestra en la lista del medio, segun la seccion.
static vector<ItemLista> construirLista(Chat& chat, Seccion seccion, bool modoGrupo,
                                        const set<string>& marcados) {
    vector<ItemLista> items;

    auto estaEnLinea = [&](const string& u) {
        return find(chat.enLinea.begin(), chat.enLinea.end(), u) != chat.enLinea.end();
    };

    if (seccion == Seccion::GENERAL) {
        for (const string& u : chat.enLinea) {
            ItemLista it;
            it.clave = u;
            it.titulo = u + (u == chat.yo ? "  (tú)" : "");
            it.subtitulo = "en línea";
            it.avatar = u;
            it.enLinea = true;
            it.clicable = false;
            items.push_back(it);
        }

    } else if (seccion == Seccion::DIRECTOS) {
        for (const string& u : chat.enLinea) {
            if (u == chat.yo) continue;

            ItemLista it;
            it.clave = u;
            it.titulo = u;
            it.subtitulo = "en línea";
            it.avatar = u;
            it.enLinea = true;
            auto d = chat.directos.find(u);
            it.nuevos = (d != chat.directos.end() && d->second.nuevos);
            items.push_back(it);
        }

        // Conversaciones con gente que ahorita no esta conectada.
        for (auto& par : chat.directos) {
            if (par.first == chat.yo || estaEnLinea(par.first)) continue;

            ItemLista it;
            it.clave = par.first;
            it.titulo = par.first;
            it.subtitulo = "desconectado";
            it.avatar = par.first;
            it.nuevos = par.second.nuevos;
            items.push_back(it);
        }

    } else if (seccion == Seccion::GRUPOS) {
        if (modoGrupo) {
            for (const string& u : chat.enLinea) {
                if (u == chat.yo) continue;

                ItemLista it;
                it.clave = u;
                it.titulo = u;
                it.subtitulo = "en línea";
                it.avatar = u;
                it.enLinea = true;
                it.conCheck = true;
                it.marcado = marcados.count(u) > 0;
                items.push_back(it);
            }
        } else {
            for (const string& id : chat.ordenGrupos) {
                const Conversacion& g = chat.grupos[id];

                ItemLista it;
                it.clave = id;
                it.titulo = g.titulo;
                it.subtitulo = to_string(g.miembros.size()) + " miembros";
                it.nuevos = g.nuevos;
                items.push_back(it);
            }
        }

    } else { // CANALES
        for (const string& id : chat.ordenCanales) {
            const Conversacion& c = chat.canales[id];

            ItemLista it;
            it.clave = id;
            it.titulo = c.titulo;
            it.subtitulo = (c.dueno == chat.yo ? "tuyo" : (c.unido ? "unido" : "sin unirte")) +
                           string(" · de ") + c.dueno;
            it.nuevos = c.unido && c.nuevos;
            items.push_back(it);
        }
    }

    return items;
}

// =====================================================================
int main(int argc, char* argv[]) {
    Ventana ventana("TELECHAT", ANCHO, ALTO);
    ventana.abrir();
    sf::RenderWindow& win = ventana.getWindow();

    Avatares avatares;
    if (!avatares.cargarPredeterminados()) {
        cerr << "Aviso: no se encontraron los avatares en recursos/avatares/." << endl;
    }

    Conexion conexion;
    unique_ptr<Chat> chat;
    Pantalla pantalla = Pantalla::LOGIN;

    // =============== LOGIN ===============
    CampoTexto campoIp("Servidor (IP o nombre)", 350, 124, 300, 42);
    CampoTexto campoPuerto("Puerto", 350, 190, 300, 42);
    CampoTexto campoUsuario("Usuario (máx. 16)", 350, 256, 300, 42);
    CampoTexto campoClave("Contraseña (mín. 4)", 350, 322, 300, 42, true);
    CampoTexto campoFoto("O usa tu propia foto: ruta de una imagen", 350, 484, 300, 42);
    Boton botonEntrar("Entrar", 350, 542, 145, 44);
    Boton botonRegistro("Registrarme", 505, 542, 145, 44);
    int avatarElegido = -1; // indice en AVATARES_PREDETERMINADOS (-1 = ninguno)

    campoIp.setMaxLongitud(60);
    campoIp.setContenido(argc > 1 ? argv[1] : "127.0.0.1");
    campoPuerto.setMaxLongitud(5);
    campoPuerto.setModo(ModoCampo::SOLO_DIGITOS);
    campoPuerto.setContenido(to_string(argc > 2 ? atoi(argv[2]) : PUERTO_POR_DEFECTO));
    campoUsuario.setMaxLongitud(MAX_NOMBRE_USUARIO);
    campoClave.setMaxLongitud(MAX_CLAVE);
    campoFoto.setMaxLongitud(300);
    vector<CampoTexto*> camposLogin = {&campoIp, &campoPuerto, &campoUsuario, &campoClave, &campoFoto};
    campoUsuario.setEnfocado(true);

    Fase fase = Fase::EDITANDO;
    Intento intento;
    int framesConectando = 0;
    sf::Clock relojLogin;
    string mensajeLogin;
    bool mensajeEsError = true;

    // =============== CHAT ===============
    Seccion seccion = Seccion::GENERAL;
    string selDirecto, selGrupo, selCanal;
    bool modoGrupo = false;       // estoy eligiendo los miembros de un grupo nuevo
    set<string> marcados;
    int scrollLista = 0;
    long long confirmarBorrado = 0; // id del mensaje que esta esperando el 2do clic de "eliminar"
    Dialogo dialogo = Dialogo::NINGUNO;
    AccionConfirmar accionConfirmar = AccionConfirmar::NINGUNA;
    string grupoAdministrado, idConfirmar, usuarioConfirmar;
    int scrollMiembros = 0;
    string canalConsultado;
    int scrollMiembrosCanal = 0;
    bool confirmarCuenta = false;

    CampoTexto campoMensaje("Mensaje", AREA_X, 560, 400, 42);
    campoMensaje.setMaxLongitud(MAX_TEXTO);
    CampoTexto campoNombreGrupo("Nombre del grupo", LISTA_X, 506, LISTA_ANCHO, 38);
    campoNombreGrupo.setMaxLongitud(MAX_NOMBRE_SALA);
    CampoTexto campoNombreCanal("Nombre del canal nuevo", LISTA_X, 506, LISTA_ANCHO, 38);
    campoNombreCanal.setMaxLongitud(MAX_NOMBRE_SALA);
    vector<CampoTexto*> camposChat = {&campoMensaje, &campoNombreGrupo, &campoNombreCanal};

    Boton botonEnviar("Enviar", 835, 560, 150, 42);
    Boton botonSalir("Salir", 15, 575, 160, 44);
    botonSalir.setColor(sf::Color(255, 205, 236));
    Boton botonSeccion[4] = {Boton("General", 15, 100, 160, 44), Boton("Directos", 15, 152, 160, 44),
                             Boton("Grupos", 15, 204, 160, 44), Boton("Canales", 15, 256, 160, 44)};
    for (Boton& b : botonSeccion) b.setColor(BOTON_LATERAL);
    Boton botonNuevoGrupo("Nuevo grupo", LISTA_X, 560, LISTA_ANCHO, 44);
    Boton botonCrearGrupo("Crear grupo", LISTA_X, 556, LISTA_ANCHO, 40);
    Boton botonCancelar("Cancelar", LISTA_X, 602, LISTA_ANCHO, 30);
    botonCancelar.setColor(sf::Color(255, 205, 236));
    Boton botonCrearCanal("Crear canal", LISTA_X, 556, LISTA_ANCHO, 44);
    Boton botonUnirse("Unirme al canal", AREA_X + AREA_ANCHO / 2 - 100, AREA_Y + AREA_ALTO / 2 + 10, 200, 46);

    // Administracion: acciones visibles segun permisos, con dialogos de confirmacion.
    // Botones en el encabezado derecho, lejos de la etiqueta "Mensaje" (y=536).
    Boton botonGestionGrupo("Administrar grupo", 790, 15, 195, 34);
    // Dos botones independientes, ambos caben en la columna derecha del encabezado.
    Boton botonVerMiembrosCanal("Ver miembros", 700, 15, 125, 34);
    Boton botonEliminarCanal("Eliminar canal", 833, 15, 152, 34);
    Boton botonEliminarCuenta("Eliminar cuenta", 15, 515, 160, 40);
    botonEliminarCuenta.setColor(sf::Color(255, 205, 236));
    CampoTexto campoNuevoMiembro("Usuario que deseas agregar", 265, 424, 325, 40);
    campoNuevoMiembro.setMaxLongitud(MAX_NOMBRE_USUARIO);
    CampoTexto campoClaveEliminar("Escribe tu contraseña para confirmar", 305, 330, 390, 44, true);
    campoClaveEliminar.setMaxLongitud(MAX_CLAVE);
    Boton botonAgregarMiembro("Agregar", 615, 424, 155, 40);
    Boton botonSalirDelGrupo("Salir del grupo", 265, 485, 190, 38);
    Boton botonEliminarGrupo("Eliminar grupo", 265, 485, 190, 38);
    Boton botonCerrarDialogo("Cerrar", 650, 485, 120, 38);
    Boton botonConfirmarSi("Confirmar", 360, 365, 130, 42);
    Boton botonConfirmarNo("Cancelar", 510, 365, 130, 42);
    Boton botonConfirmarCuenta("Eliminar mi cuenta", 335, 393, 330, 43);
    Boton botonCancelarCuenta("Cancelar", 425, 455, 150, 40);

    // ---------------- acciones ----------------

    auto mostrarError = [&](const string& texto) {
        mensajeLogin = texto;
        mensajeEsError = true;
    };

    // Valida los campos, prepara la foto (si es registro) y pasa a "CONECTANDO".
    auto iniciarIntento = [&](bool registro) {
        string ip = quitarEspacios(campoIp.getContenido());
        int puerto = campoPuerto.getContenido().empty() ? 0 : atoi(campoPuerto.getContenido().c_str());
        string usuario = quitarEspacios(campoUsuario.getContenido());
        string clave = campoClave.getContenido();

        if (ip.empty()) { mostrarError("Escribe la IP del servidor."); return; }
        if (puerto <= 0 || puerto > 65535) { mostrarError("Puerto no válido (1-65535)."); return; }
        if (usuario.empty()) { mostrarError("Escribe un nombre de usuario."); return; }
        if (usuario.find('|') != string::npos || usuario.find(',') != string::npos) {
            mostrarError("El usuario no puede llevar '|' ni ','.");
            return;
        }
        if (clave.empty()) { mostrarError("Escribe tu contraseña."); return; }

        intento = Intento();
        intento.registro = registro;
        intento.ip = ip;
        intento.puerto = puerto;
        intento.usuario = usuario;
        intento.clave = clave;

        if (registro) {
            if (clave.size() < MIN_CLAVE) {
                mostrarError("La contraseña debe tener al menos " + to_string(MIN_CLAVE) + " caracteres.");
                return;
            }

            string rutaFoto = quitarEspacios(campoFoto.getContenido());
            string error;

            if (!rutaFoto.empty()) {               // la foto propia tiene prioridad
                if (!prepararFoto(rutaFoto, intento.foto, error)) {
                    mostrarError(error);
                    return;
                }
            } else if (avatarElegido >= 0) {       // o el animalito que eligio
                intento.foto = string("animal:") + AVATARES_PREDETERMINADOS[avatarElegido];
            }
        }

        mensajeLogin = "Conectando...";
        mensajeEsError = false;
        fase = Fase::CONECTANDO;
        framesConectando = 0; // se espera un par de cuadros para que el mensaje alcance a dibujarse
    };

    // Se conecta (esto bloquea hasta 3 s) y manda LOGIN o REGISTRO.
    auto ejecutarConexion = [&]() {
        string error;

        if (!conexion.conectar(intento.ip, static_cast<unsigned short>(intento.puerto), 3000, error)) {
            mostrarError(error);
            fase = Fase::EDITANDO;
            return;
        }

        if (intento.registro) {
            conexion.enviar("REGISTRO|" + intento.usuario + "|" + intento.clave + "|" + intento.foto);
        } else {
            conexion.enviar("LOGIN|" + intento.usuario + "|" + intento.clave);
        }

        mensajeLogin = intento.registro ? "Creando tu cuenta..." : "Iniciando sesión...";
        fase = Fase::ESPERANDO;
        relojLogin.restart();
    };

    auto manejarLineaLogin = [&](const string& linea) {
        vector<string> c = separarMaximo(linea, 2);

        if (c[0] == "OK" && c.size() >= 2) {
            avatares.limpiarUsuarios();
            chat = make_unique<Chat>(ventana, avatares);
            chat->yo = c[1];
            pantalla = Pantalla::CHAT;
            seccion = Seccion::GENERAL;
            enfocar(camposChat, &campoMensaje);
            campoClave.limpiar();
            fase = Fase::EDITANDO;

        } else if (c[0] == "ERR") {
            mostrarError(c.size() >= 2 ? c[1] : "El servidor rechazó la conexión.");
            conexion.cerrar();
            fase = Fase::EDITANDO;
        }
    };

    // La conversacion que se ve en el panel derecho (nullptr si no hay una elegida).
    auto conversacionActual = [&]() -> Conversacion* {
        if (!chat) return nullptr;

        switch (seccion) {
            case Seccion::GENERAL:
                return &chat->general;
            case Seccion::DIRECTOS:
                return selDirecto.empty() ? nullptr : &chat->directo(selDirecto);
            case Seccion::GRUPOS: {
                auto it = chat->grupos.find(selGrupo);
                return it == chat->grupos.end() ? nullptr : &it->second;
            }
            case Seccion::CANALES: {
                auto it = chat->canales.find(selCanal);
                return it == chat->canales.end() ? nullptr : &it->second;
            }
        }

        return nullptr;
    };

    // Id del mensaje cuyo "eliminar" esta en (mx, my); 0 si no hay ninguno.
    auto idEliminarEn = [&](const Conversacion* conv, float mx, float my) -> long long {
        if (conv == nullptr || !conexion.estaActiva()) return 0;
        if (seccion == Seccion::CANALES && !conv->unido) return 0;

        const int total = static_cast<int>(conv->lineas.size());
        const int inicio = max(0, total - LINEAS_VISIBLES - conv->desplazamiento);

        for (int k = 0; k < LINEAS_VISIBLES && inicio + k < total; k++) {
            const Linea& l = conv->lineas[static_cast<size_t>(inicio + k)];
            if (l.idMensaje == 0 || !l.esMio) continue;

            float y = AREA_Y + 6.0f + static_cast<float>(k) * ALTO_LINEA;

            if (mx >= BORRAR_X && mx < BORRAR_X + BORRAR_ANCHO && my >= y && my < y + ALTO_LINEA) {
                return l.idMensaje;
            }
        }

        return 0;
    };

    // En canales solo escribe el dueño; en lo demas, cualquiera con la conversacion abierta.
    auto puedeEscribir = [&](const Conversacion* conv) {
        if (conv == nullptr || !conexion.estaActiva()) return false;
        if (seccion == Seccion::CANALES) return conv->unido && conv->dueno == chat->yo;
        return true;
    };

    auto enviarTexto = [&]() {
        Conversacion* conv = conversacionActual();
        string texto = quitarEspacios(campoMensaje.getContenido());

        if (texto.empty() || !puedeEscribir(conv)) return;

        switch (seccion) {
            case Seccion::GENERAL:  conexion.enviar("GENERAL|" + texto); break;
            case Seccion::DIRECTOS: conexion.enviar("DM|" + conv->id + "|" + texto); break;
            case Seccion::GRUPOS:   conexion.enviar("GMSG|" + conv->id + "|" + texto); break;
            case Seccion::CANALES:  conexion.enviar("CMSG|" + conv->id + "|" + texto); break;
        }

        campoMensaje.limpiar();
        conv->desplazamiento = 0; // al mandar, regresar a lo mas reciente
    };

    auto crearGrupo = [&]() {
        string nombre = quitarEspacios(campoNombreGrupo.getContenido());

        if (nombre.empty()) { chat->aviso = "Ponle un nombre al grupo."; chat->relojAviso.restart(); return; }
        if (marcados.empty()) { chat->aviso = "Marca al menos un usuario."; chat->relojAviso.restart(); return; }

        string miembros;
        for (const string& m : marcados) miembros += (miembros.empty() ? "" : ",") + m;

        conexion.enviar("GRUPO_NUEVO|" + nombre + "|" + miembros);
        campoNombreGrupo.limpiar();
        marcados.clear();
        modoGrupo = false;
        scrollLista = 0;
    };

    auto crearCanal = [&]() {
        string nombre = quitarEspacios(campoNombreCanal.getContenido());
        if (nombre.empty()) return;

        conexion.enviar("CANAL_NUEVO|" + nombre);
        campoNombreCanal.limpiar();
    };

    auto abrirConfirmacion = [&](AccionConfirmar accion, const string& id, const string& usuario = "") {
        accionConfirmar = accion;
        idConfirmar = id;
        usuarioConfirmar = usuario;
        dialogo = Dialogo::CONFIRMAR;
    };

    auto confirmarAccion = [&]() {
        switch (accionConfirmar) {
            case AccionConfirmar::QUITAR_MIEMBRO:
                conexion.enviar("GRUPO_QUITAR|" + idConfirmar + "|" + usuarioConfirmar);
                break;
            case AccionConfirmar::SALIR_GRUPO:
                conexion.enviar("GRUPO_SALIR|" + idConfirmar);
                break;
            case AccionConfirmar::ELIMINAR_GRUPO:
                conexion.enviar("GRUPO_ELIMINAR|" + idConfirmar);
                break;
            case AccionConfirmar::ELIMINAR_CANAL:
                conexion.enviar("CANAL_ELIMINAR|" + idConfirmar);
                break;
            default: break;
        }
        dialogo = Dialogo::NINGUNO;
        accionConfirmar = AccionConfirmar::NINGUNA;
    };

    auto cambiarSeccion = [&](Seccion nueva) {
        seccion = nueva;
        modoGrupo = false;
        marcados.clear();
        scrollLista = 0;
        dialogo = Dialogo::NINGUNO;
        enfocar(camposChat, &campoMensaje);
    };

    // Campos de texto que estan en pantalla ahora mismo (para Tab, clics y teclas).
    auto camposVisibles = [&]() {
        if (dialogo == Dialogo::CUENTA) return vector<CampoTexto*>{&campoClaveEliminar};
        if (dialogo == Dialogo::ADMIN_GRUPO) {
            auto it = chat->grupos.find(grupoAdministrado);
            if (it != chat->grupos.end() && it->second.dueno == chat->yo)
                return vector<CampoTexto*>{&campoNuevoMiembro};
            return vector<CampoTexto*>{};
        }
        if (dialogo == Dialogo::CONFIRMAR || dialogo == Dialogo::MIEMBROS_CANAL)
            return vector<CampoTexto*>{};
        vector<CampoTexto*> v = {&campoMensaje};
        if (seccion == Seccion::GRUPOS && modoGrupo) v.push_back(&campoNombreGrupo);
        if (seccion == Seccion::CANALES) v.push_back(&campoNombreCanal);
        return v;
    };

    // Hasta donde llega la lista del medio (abajo hay botones en Grupos y Canales).
    auto listaFondo = [&]() {
        bool conAcciones = (seccion == Seccion::GRUPOS || seccion == Seccion::CANALES);
        return conAcciones ? 470.0f : static_cast<float>(ALTO) - 10.0f;
    };

    // =================== CICLO PRINCIPAL ===================
    while (win.isOpen()) {

        // ---------- 1) EVENTOS ----------
        sf::Event e;

        while (win.pollEvent(e)) {
            if (e.type == sf::Event::Closed) {
                win.close();
                continue;
            }

            bool pegar = (e.type == sf::Event::KeyPressed && e.key.code == sf::Keyboard::V &&
                          (e.key.control || e.key.system));

            if (pantalla == Pantalla::LOGIN) {
                if (fase != Fase::EDITANDO) continue; // mientras conecta no se toca nada

                if (pegar) {
                    pegarEnCampos(camposLogin);
                } else if (e.type == sf::Event::TextEntered) {
                    teclearEnCampos(camposLogin, e.text.unicode);
                } else if (e.type == sf::Event::KeyPressed) {
                    if (e.key.code == sf::Keyboard::Tab) siguienteCampo(camposLogin);
                    if (e.key.code == sf::Keyboard::Return) iniciarIntento(false);
                } else if (e.type == sf::Event::MouseButtonPressed && e.mouseButton.button == sf::Mouse::Left) {
                    float mx = static_cast<float>(e.mouseButton.x);
                    float my = static_cast<float>(e.mouseButton.y);

                    clicEnCampos(camposLogin, mx, my);

                    for (int i = 0; i < NUM_AVATARES; i++) { // elegir / quitar un animalito
                        float dx = mx - (AVATAR_X + static_cast<float>(i) * AVATAR_PASO + AVATAR_D / 2);
                        float dy = my - (AVATAR_Y + AVATAR_D / 2);
                        if (dx * dx + dy * dy <= (AVATAR_D / 2) * (AVATAR_D / 2)) {
                            avatarElegido = (avatarElegido == i) ? -1 : i;
                        }
                    }

                    if (botonEntrar.contiene(mx, my)) iniciarIntento(false);
                    if (botonRegistro.contiene(mx, my)) iniciarIntento(true);
                }
                continue;
            }

            // ----- pantalla de CHAT -----
            vector<CampoTexto*> visibles = camposVisibles();
            Conversacion* conv = conversacionActual();

            // Los dialogos interceptan los eventos para no tocar accidentalmente el chat.
            if (dialogo != Dialogo::NINGUNO) {
                if (pegar) {
                    pegarEnCampos(visibles);
                } else if (e.type == sf::Event::TextEntered) {
                    teclearEnCampos(visibles, e.text.unicode);
                    if (dialogo == Dialogo::CUENTA) confirmarCuenta = false;
                } else if (e.type == sf::Event::KeyPressed) {
                    if (e.key.code == sf::Keyboard::Escape) {
                        dialogo = Dialogo::NINGUNO;
                        campoClaveEliminar.limpiar();
                    } else if (e.key.code == sf::Keyboard::Return && dialogo == Dialogo::ADMIN_GRUPO) {
                        auto it = chat->grupos.find(grupoAdministrado);
                        if (it != chat->grupos.end() && it->second.dueno == chat->yo &&
                            !quitarEspacios(campoNuevoMiembro.getContenido()).empty()) {
                            conexion.enviar("GRUPO_AGREGAR|" + grupoAdministrado + "|" + quitarEspacios(campoNuevoMiembro.getContenido()));
                            campoNuevoMiembro.limpiar();
                        }
                    }
                } else if (e.type == sf::Event::MouseWheelScrolled && dialogo == Dialogo::ADMIN_GRUPO) {
                    auto it = chat->grupos.find(grupoAdministrado);
                    if (it != chat->grupos.end()) {
                        scrollMiembros = max(0, min(static_cast<int>(it->second.miembros.size()) - 5,
                            scrollMiembros - (e.mouseWheelScroll.delta > 0 ? 1 : -1)));
                    }
                } else if (e.type == sf::Event::MouseWheelScrolled && dialogo == Dialogo::MIEMBROS_CANAL) {
                    auto it = chat->canales.find(canalConsultado);
                    if (it != chat->canales.end()) {
                        scrollMiembrosCanal = max(0, min(max(0, static_cast<int>(it->second.miembros.size()) - 7),
                            scrollMiembrosCanal - (e.mouseWheelScroll.delta > 0 ? 1 : -1)));
                    }
                } else if (e.type == sf::Event::MouseButtonPressed && e.mouseButton.button == sf::Mouse::Left) {
                    float mx = static_cast<float>(e.mouseButton.x);
                    float my = static_cast<float>(e.mouseButton.y);
                    clicEnCampos(visibles, mx, my);

                    if (dialogo == Dialogo::ADMIN_GRUPO) {
                        auto it = chat->grupos.find(grupoAdministrado);
                        if (it == chat->grupos.end()) { dialogo = Dialogo::NINGUNO; continue; }
                        const Conversacion& g = it->second;
                        bool dueno = (g.dueno == chat->yo);
                        if (botonCerrarDialogo.contiene(mx, my)) {
                            dialogo = Dialogo::NINGUNO;
                        } else if (dueno && botonEliminarGrupo.contiene(mx, my)) {
                            abrirConfirmacion(AccionConfirmar::ELIMINAR_GRUPO, grupoAdministrado);
                        } else if (!dueno && botonSalirDelGrupo.contiene(mx, my)) {
                            abrirConfirmacion(AccionConfirmar::SALIR_GRUPO, grupoAdministrado);
                        } else if (dueno && botonAgregarMiembro.contiene(mx, my)) {
                            string nuevo = quitarEspacios(campoNuevoMiembro.getContenido());
                            if (!nuevo.empty()) {
                                conexion.enviar("GRUPO_AGREGAR|" + grupoAdministrado + "|" + nuevo);
                                campoNuevoMiembro.limpiar();
                            }
                        } else if (dueno && mx >= 660 && mx < 780 && my >= 215 && my < 385) {
                            int fila = static_cast<int>((my - 215) / 34) + scrollMiembros;
                            if (fila >= 0 && fila < static_cast<int>(g.miembros.size()) &&
                                g.miembros[static_cast<size_t>(fila)] != g.dueno) {
                                abrirConfirmacion(AccionConfirmar::QUITAR_MIEMBRO, grupoAdministrado,
                                                   g.miembros[static_cast<size_t>(fila)]);
                            }
                        }
                    } else if (dialogo == Dialogo::MIEMBROS_CANAL) {
                        if (botonCerrarDialogo.contiene(mx, my)) dialogo = Dialogo::NINGUNO;
                    } else if (dialogo == Dialogo::CONFIRMAR) {
                        if (botonConfirmarNo.contiene(mx, my)) {
                            dialogo = (accionConfirmar == AccionConfirmar::QUITAR_MIEMBRO ||
                                       accionConfirmar == AccionConfirmar::SALIR_GRUPO ||
                                       accionConfirmar == AccionConfirmar::ELIMINAR_GRUPO) ? Dialogo::ADMIN_GRUPO : Dialogo::NINGUNO;
                        } else if (botonConfirmarSi.contiene(mx, my)) {
                            confirmarAccion();
                        }
                    } else if (dialogo == Dialogo::CUENTA) {
                        if (botonCancelarCuenta.contiene(mx, my)) {
                            dialogo = Dialogo::NINGUNO;
                            confirmarCuenta = false;
                            campoClaveEliminar.limpiar();
                        } else if (botonConfirmarCuenta.contiene(mx, my)) {
                            if (campoClaveEliminar.getContenido().empty()) {
                                chat->aviso = "Escribe tu contrasena para eliminar la cuenta.";
                                chat->relojAviso.restart();
                            } else if (!confirmarCuenta) {
                                confirmarCuenta = true;
                            } else {
                                conexion.enviar("CUENTA_ELIMINAR|" + campoClaveEliminar.getContenido());
                                campoClaveEliminar.limpiar();
                                confirmarCuenta = false;
                                dialogo = Dialogo::NINGUNO;
                            }
                        }
                    }
                }
                continue;
            }

            if (pegar) {
                pegarEnCampos(visibles);

            } else if (e.type == sf::Event::TextEntered) {
                teclearEnCampos(visibles, e.text.unicode);

            } else if (e.type == sf::Event::KeyPressed) {
                switch (e.key.code) {
                    case sf::Keyboard::Return:
                        if (modoGrupo && campoNombreGrupo.estaEnfocado()) crearGrupo();
                        else if (seccion == Seccion::CANALES && campoNombreCanal.estaEnfocado()) crearCanal();
                        else enviarTexto();
                        break;
                    case sf::Keyboard::Tab:      siguienteCampo(visibles); break;
                    case sf::Keyboard::PageUp:   if (conv) conv->desplazamiento += LINEAS_VISIBLES - 1; break;
                    case sf::Keyboard::PageDown: if (conv) conv->desplazamiento -= LINEAS_VISIBLES - 1; break;
                    case sf::Keyboard::Up:       if (conv) conv->desplazamiento += 1; break;
                    case sf::Keyboard::Down:     if (conv) conv->desplazamiento -= 1; break;
                    case sf::Keyboard::End:      if (conv) conv->desplazamiento = 0; break;
                    default: break;
                }

            } else if (e.type == sf::Event::MouseWheelScrolled) {
                float mx = static_cast<float>(e.mouseWheelScroll.x);
                int pasos = static_cast<int>(e.mouseWheelScroll.delta * 3.0f);

                if (mx >= LISTA_X && mx < LISTA_X + LISTA_ANCHO) {
                    scrollLista -= (pasos > 0 ? 1 : (pasos < 0 ? -1 : 0)); // la lista se mueve de a una fila
                } else if (conv) {
                    conv->desplazamiento += pasos;
                }

            } else if (e.type == sf::Event::MouseButtonPressed && e.mouseButton.button == sf::Mouse::Left) {
                float mx = static_cast<float>(e.mouseButton.x);
                float my = static_cast<float>(e.mouseButton.y);

                clicEnCampos(visibles, mx, my);

                // -- eliminar un mensaje mio: el primer clic pide confirmacion, el segundo lo borra --
                long long idBorrar = idEliminarEn(conv, mx, my);

                if (idBorrar != 0 && confirmarBorrado == idBorrar) {
                    conexion.enviar("BORRAR|" + to_string(idBorrar));
                    confirmarBorrado = 0;
                } else {
                    confirmarBorrado = idBorrar; // 0 si el clic fue en cualquier otro lado
                }

                // -- barra lateral --
                for (int i = 0; i < 4; i++) {
                    if (botonSeccion[i].contiene(mx, my)) cambiarSeccion(static_cast<Seccion>(i));
                }
                if (botonSalir.contiene(mx, my)) win.close();
                if (botonEliminarCuenta.contiene(mx, my) && conexion.estaActiva()) {
                    dialogo = Dialogo::CUENTA;
                    confirmarCuenta = false;
                    campoClaveEliminar.limpiar();
                    enfocar(camposChat, nullptr);
                    campoClaveEliminar.setEnfocado(true);
                }

                // -- lista del medio --
                if (mx >= LISTA_X && mx < LISTA_X + LISTA_ANCHO && my >= LISTA_Y && my < listaFondo()) {
                    vector<ItemLista> items = construirLista(*chat, seccion, modoGrupo, marcados);
                    int fila = static_cast<int>((my - LISTA_Y) / FILA_ALTO) + scrollLista;

                    if (fila >= 0 && fila < static_cast<int>(items.size()) && items[static_cast<size_t>(fila)].clicable) {
                        const ItemLista& it = items[static_cast<size_t>(fila)];

                        if (it.conCheck) {
                            if (!marcados.erase(it.clave)) marcados.insert(it.clave);
                        } else if (seccion == Seccion::DIRECTOS) {
                            selDirecto = it.clave;
                            chat->directo(it.clave).nuevos = false;
                            enfocar(camposChat, &campoMensaje);
                        } else if (seccion == Seccion::GRUPOS) {
                            selGrupo = it.clave;
                            enfocar(camposChat, &campoMensaje);
                        } else if (seccion == Seccion::CANALES) {
                            selCanal = it.clave;
                            enfocar(camposChat, &campoMensaje);
                        }
                    }
                }

                // -- botones de abajo de la lista --
                if (seccion == Seccion::GRUPOS) {
                    if (!modoGrupo && botonNuevoGrupo.contiene(mx, my)) {
                        modoGrupo = true;
                        scrollLista = 0;
                    } else if (modoGrupo && botonCrearGrupo.contiene(mx, my)) {
                        crearGrupo();
                    } else if (modoGrupo && botonCancelar.contiene(mx, my)) {
                        modoGrupo = false;
                        marcados.clear();
                        scrollLista = 0;
                    }
                } else if (seccion == Seccion::CANALES && botonCrearCanal.contiene(mx, my)) {
                    crearCanal();
                }

                // -- panel derecho --
                if (botonEnviar.contiene(mx, my)) enviarTexto();
                if (seccion == Seccion::GRUPOS && conv && botonGestionGrupo.contiene(mx, my)) {
                    grupoAdministrado = conv->id;
                    scrollMiembros = 0;
                    campoNuevoMiembro.limpiar();
                    dialogo = Dialogo::ADMIN_GRUPO;
                    enfocar(camposChat, nullptr);
                    campoNuevoMiembro.setEnfocado(true);
                }
                if (seccion == Seccion::CANALES && conv && conv->dueno == chat->yo) {
                    if (botonVerMiembrosCanal.contiene(mx, my)) {
                        canalConsultado = conv->id;
                        scrollMiembrosCanal = 0;
                        conv->miembros.clear();
                        conv->miembrosCanalCargados = false;
                        dialogo = Dialogo::MIEMBROS_CANAL;
                        conexion.enviar("CANAL_MIEMBROS_PEDIR|" + conv->id);
                    } else if (botonEliminarCanal.contiene(mx, my)) {
                        abrirConfirmacion(AccionConfirmar::ELIMINAR_CANAL, conv->id);
                    }
                }

                if (seccion == Seccion::CANALES && conv && !conv->unido && botonUnirse.contiene(mx, my)) {
                    conexion.enviar("CANAL_UNIR|" + conv->id);
                }
            }
        }

        // ---------- 2) RED ----------
        conexion.actualizar();

        if (pantalla == Pantalla::LOGIN && fase == Fase::CONECTANDO && ++framesConectando >= 2) {
            ejecutarConexion();
        }

        string linea;
        while (conexion.siguienteLinea(linea)) {
            if (pantalla == Pantalla::LOGIN) {
                if (fase == Fase::ESPERANDO) manejarLineaLogin(linea);
            } else if (linea == "CUENTA_ELIMINADA") {
                // El servidor ya confirmo el borrado en SQLite.
                conexion.cerrar();
                chat.reset();
                pantalla = Pantalla::LOGIN;
                fase = Fase::EDITANDO;
                mensajeLogin = "Tu cuenta se elimino correctamente.";
                mensajeEsError = false;
                campoClave.limpiar();
                campoClaveEliminar.limpiar();
                dialogo = Dialogo::NINGUNO;
                selGrupo.clear(); selCanal.clear(); selDirecto.clear();
            } else {
                chat->procesar(linea);
                if ((dialogo == Dialogo::ADMIN_GRUPO && !chat->grupos.count(grupoAdministrado)) ||
                    (dialogo == Dialogo::MIEMBROS_CANAL && !chat->canales.count(canalConsultado)))
                    dialogo = Dialogo::NINGUNO;
            }
        }

        if (pantalla == Pantalla::LOGIN && fase == Fase::ESPERANDO) {
            if (!conexion.estaActiva()) {
                mostrarError("El servidor cerró la conexión.");
                fase = Fase::EDITANDO;
            } else if (relojLogin.getElapsedTime().asSeconds() > 8.0f) {
                mostrarError("El servidor no respondió a tiempo.");
                conexion.cerrar();
                fase = Fase::EDITANDO;
            }
        }

        if (pantalla == Pantalla::CHAT) {
            for (const string& u : chat->fotosPorPedir()) {
                conexion.enviar("FOTO_PEDIR|" + u);
            }

            Conversacion* conv = conversacionActual();

            if (conv) {
                int maxDesp = max(0, static_cast<int>(conv->lineas.size()) - LINEAS_VISIBLES);
                conv->desplazamiento = max(0, min(conv->desplazamiento, maxDesp));
                conv->nuevos = false; // la estoy viendo
                if (conexion.estaActiva() && conv->ultimoId > conv->ultimoLeido &&
                    (seccion != Seccion::CANALES || conv->unido)) {
                    string ambito = seccion == Seccion::GENERAL ? "G" :
                                    seccion == Seccion::DIRECTOS ? "D" :
                                    seccion == Seccion::GRUPOS ? "R" : "C";
                    string clave = seccion == Seccion::GENERAL ? "general" : conv->id;
                    conexion.enviar("MARCAR_LEIDO|" + ambito + "|" + clave + "|" + to_string(conv->ultimoId));
                    conv->ultimoLeido = conv->ultimoId;
                }
            }
        }

        sf::Vector2i mouse = sf::Mouse::getPosition(win);
        float mx = static_cast<float>(mouse.x);
        float my = static_cast<float>(mouse.y);

        // ---------- 3) DIBUJAR ----------
        win.clear(FONDO);

        if (pantalla == Pantalla::LOGIN) {
            botonEntrar.actualizarHover(mx, my);
            botonRegistro.actualizarHover(mx, my);

            ventana.dibujarTextoCentrado("TELECHAT", sf::FloatRect(275, 16, 450, 42), 34, TEXTO_OSCURO);
            ventana.dibujarTexto("Entra con tu cuenta o créate una nueva", 332, 62, 15, TEXTO_SUAVE);

            for (CampoTexto* c : camposLogin) c->dibujar(ventana);

            // Selector de avatar (animalitos predeterminados)
            ventana.dibujarTexto("Elige un avatar (opcional, solo al registrarte)", AVATAR_X, 370, 15, TEXTO_SUAVE);

            for (int i = 0; i < NUM_AVATARES; i++) {
                float ax = AVATAR_X + static_cast<float>(i) * AVATAR_PASO;
                float dx = mx - (ax + AVATAR_D / 2), dy = my - (AVATAR_Y + AVATAR_D / 2);
                bool encima = dx * dx + dy * dy <= (AVATAR_D / 2) * (AVATAR_D / 2);

                if (i == avatarElegido || encima) { // aro detras del avatar
                    sf::CircleShape aro(AVATAR_D / 2 + 4.0f);
                    aro.setPosition(ax - 4.0f, AVATAR_Y - 4.0f);
                    aro.setFillColor(i == avatarElegido ? ACENTO : SELECCION);
                    win.draw(aro);
                }

                avatares.dibujarPredeterminado(ventana, AVATARES_PREDETERMINADOS[i], ax, AVATAR_Y, AVATAR_D);
            }

            botonEntrar.dibujar(ventana);
            botonRegistro.dibujar(ventana);

            ventana.dibujarTexto(mensajeLogin, 350, 594, 15, mensajeEsError ? ROJO_ERROR : TEXTO_SUAVE);
            ventana.dibujarTexto("Tab: cambiar de campo    Enter: entrar    Ctrl+V: pegar", 350, 618, 12, TEXTO_SUAVE);

        } else {
            Chat& c = *chat;
            Conversacion* conv = conversacionActual();

            for (int i = 0; i < 4; i++) {
                botonSeccion[i].setActivo(static_cast<int>(seccion) == i);
                botonSeccion[i].actualizarHover(mx, my);
            }

            // Marca "•" si hay mensajes sin leer en una seccion.
            bool nuevosDirectos = false, nuevosGrupos = false, nuevosCanales = false;
            for (auto& p : c.directos) nuevosDirectos = nuevosDirectos || (p.second.nuevos && !(seccion == Seccion::DIRECTOS && p.first == selDirecto));
            for (auto& p : c.grupos) nuevosGrupos = nuevosGrupos || (p.second.nuevos && !(seccion == Seccion::GRUPOS && p.first == selGrupo));
            for (auto& p : c.canales) nuevosCanales = nuevosCanales || (p.second.unido && p.second.nuevos && !(seccion == Seccion::CANALES && p.first == selCanal));

            botonSeccion[0].setTexto(string("General") + ((c.general.nuevos && seccion != Seccion::GENERAL) ? "  •" : ""));
            botonSeccion[1].setTexto(string("Directos") + (nuevosDirectos ? "  •" : ""));
            botonSeccion[2].setTexto(string("Grupos") + (nuevosGrupos ? "  •" : ""));
            botonSeccion[3].setTexto(string("Canales") + (nuevosCanales ? "  •" : ""));

            botonSalir.actualizarHover(mx, my);
            botonEnviar.actualizarHover(mx, my);
            botonEliminarCuenta.actualizarHover(mx, my);

            // ---- barra lateral ----
            rectangulo(ventana, 0, 0, 190, ALTO, BARRA_LATERAL);
            c.avatares.dibujar(ventana, c.yo, 15, 18, 44);
            ventana.dibujarTexto(c.yo, 70, 20, 16, TEXTO_OSCURO);
            ventana.dibujarTexto(conexion.estaActiva() ? "en línea" : "sin conexión", 70, 44, 13,
                                 conexion.estaActiva() ? VERDE : ROJO_ERROR);
            for (int i = 0; i < 4; i++) botonSeccion[i].dibujar(ventana);
            botonSalir.dibujar(ventana);
            botonEliminarCuenta.dibujar(ventana);

            // ---- lista del medio ----
            rectangulo(ventana, 190, 0, 230, ALTO, PANEL_LISTA);

            const char* titulos[4] = {"En línea", "Mensajes directos", "Grupos", "Canales"};
            string tituloLista = titulos[static_cast<int>(seccion)];
            if (seccion == Seccion::GRUPOS && modoGrupo) tituloLista = "Elige los miembros";
            ventana.dibujarTexto(tituloLista, LISTA_X, 22, 18, TEXTO_OSCURO);

            vector<ItemLista> items = construirLista(c, seccion, modoGrupo, marcados);
            const float fondoLista = listaFondo();
            const int filasVisibles = max(1, static_cast<int>((fondoLista - LISTA_Y) / FILA_ALTO));
            scrollLista = max(0, min(scrollLista, max(0, static_cast<int>(items.size()) - filasVisibles)));

            if (items.empty()) {
                string vacio = "Nadie conectado.";
                if (seccion == Seccion::GRUPOS) vacio = modoGrupo ? "No hay nadie más conectado." : "Aún no tienes grupos.";
                if (seccion == Seccion::CANALES) vacio = "Aún no hay canales.";
                if (seccion == Seccion::DIRECTOS) vacio = "No hay nadie más conectado.";
                ventana.dibujarTexto(vacio, LISTA_X, LISTA_Y + 10, 14, TEXTO_SUAVE);
            }

            for (int k = 0; k < filasVisibles && k + scrollLista < static_cast<int>(items.size()); k++) {
                const ItemLista& it = items[static_cast<size_t>(k + scrollLista)];
                float y = LISTA_Y + static_cast<float>(k) * FILA_ALTO;

                bool seleccionado = !it.conCheck &&
                    ((seccion == Seccion::DIRECTOS && it.clave == selDirecto) ||
                     (seccion == Seccion::GRUPOS && it.clave == selGrupo) ||
                     (seccion == Seccion::CANALES && it.clave == selCanal));

                bool hover = it.clicable && mx >= LISTA_X && mx < LISTA_X + LISTA_ANCHO && my >= y && my < y + FILA_ALTO;

                if (seleccionado || hover) {
                    rectangulo(ventana, LISTA_X - 6, y, LISTA_ANCHO + 6, FILA_ALTO - 4,
                               seleccionado ? SELECCION : sf::Color(255, 208, 238));
                }

                float xTexto = LISTA_X + 4;

                if (it.conCheck) {
                    rectangulo(ventana, LISTA_X, y + 12, 18, 18, sf::Color(190, 120, 170));
                    rectangulo(ventana, LISTA_X + 2, y + 14, 14, 14, it.marcado ? VERDE : sf::Color::White);
                    xTexto = LISTA_X + 28;
                }

                if (!it.avatar.empty()) {
                    c.avatares.dibujar(ventana, it.avatar, xTexto, y + 3, 32);
                    xTexto += 40;
                }

                ventana.dibujarTexto(it.titulo, xTexto, y + 3, 15, TEXTO_OSCURO);
                ventana.dibujarTexto(it.subtitulo, xTexto, y + 22, 12, it.enLinea ? VERDE : TEXTO_SUAVE);

                if (it.nuevos) {
                    sf::CircleShape punto(5.0f);
                    punto.setPosition(LISTA_X + LISTA_ANCHO - 14, y + 14);
                    punto.setFillColor(ACENTO);
                    win.draw(punto);
                }
            }

            // ---- botones de abajo de la lista ----
            if (seccion == Seccion::GRUPOS) {
                if (modoGrupo) {
                    botonCrearGrupo.actualizarHover(mx, my);
                    botonCancelar.actualizarHover(mx, my);
                    campoNombreGrupo.dibujar(ventana);
                    botonCrearGrupo.dibujar(ventana);
                    botonCancelar.dibujar(ventana);
                } else {
                    botonNuevoGrupo.actualizarHover(mx, my);
                    botonNuevoGrupo.dibujar(ventana);
                }
            } else if (seccion == Seccion::CANALES) {
                botonCrearCanal.actualizarHover(mx, my);
                campoNombreCanal.dibujar(ventana);
                botonCrearCanal.dibujar(ventana);
            }

            // ---- encabezado del chat ----
            string titulo, subtitulo;

            switch (seccion) {
                case Seccion::GENERAL:
                    titulo = "Chat general";
                    subtitulo = "Todos los usuarios conectados ven estos mensajes";
                    break;
                case Seccion::DIRECTOS:
                    titulo = conv ? "Chat con " + conv->titulo : "Mensajes directos";
                    subtitulo = conv ? "Conversación privada entre ustedes dos" : "Elige a alguien de la lista";
                    break;
                case Seccion::GRUPOS:
                    titulo = conv ? conv->titulo : "Grupos";
                    if (conv) {
                        for (size_t i = 0; i < conv->miembros.size(); i++) {
                            subtitulo += (i ? ", " : "") + conv->miembros[i];
                        }
                        if (subtitulo.size() > 70) subtitulo = subtitulo.substr(0, 67) + "...";
                        subtitulo = "Miembros: " + subtitulo;
                    } else {
                        subtitulo = "Elige un grupo o crea uno nuevo";
                    }
                    break;
                case Seccion::CANALES:
                    titulo = conv ? conv->titulo : "Canales";
                    subtitulo = conv ? "Canal de " + conv->dueno + " · solo el dueño escribe"
                                     : "Elige un canal, únete o crea el tuyo";
                    break;
            }

            // Reservamos la columna derecha del encabezado para las acciones.
            // Se acorta cualquier título/subtítulo largo para que no invada los botones.
            bool hayAccionSala = conexion.estaActiva() && conv &&
                (seccion == Seccion::GRUPOS || (seccion == Seccion::CANALES && conv->dueno == c.yo));
            if (hayAccionSala) {
                const float maxTexto = (seccion == Seccion::CANALES) ? 260.0f : 350.0f;
                titulo = acortarTexto(ventana, titulo, 24, maxTexto);
                subtitulo = acortarTexto(ventana, subtitulo, 13, maxTexto);
            }
            ventana.dibujarTexto(titulo, AREA_X, 14, 24, TEXTO_OSCURO);
            ventana.dibujarTexto(subtitulo, AREA_X, 44, 13, TEXTO_SUAVE);

            // Las acciones quedan en el encabezado, NO encima del campo "Mensaje".
            if (conexion.estaActiva() && seccion == Seccion::GRUPOS && conv) {
                botonGestionGrupo.actualizarHover(mx, my);
                botonGestionGrupo.dibujar(ventana);
            }
            if (conexion.estaActiva() && seccion == Seccion::CANALES && conv && conv->dueno == c.yo) {
                botonVerMiembrosCanal.actualizarHover(mx, my);
                botonEliminarCanal.actualizarHover(mx, my);
                botonVerMiembrosCanal.dibujar(ventana);
                botonEliminarCanal.dibujar(ventana);
            }

            // ---- zona de mensajes ----
            sf::RectangleShape zona(sf::Vector2f(AREA_ANCHO, AREA_ALTO));
            zona.setPosition(AREA_X, AREA_Y);
            zona.setFillColor(sf::Color::White);
            zona.setOutlineThickness(2.0f);
            zona.setOutlineColor(BORDE);
            win.draw(zona);

            bool mostrarMensajes = conv != nullptr && (seccion != Seccion::CANALES || conv->unido);

            if (mostrarMensajes) {
                const int total = static_cast<int>(conv->lineas.size());
                const int inicio = max(0, total - LINEAS_VISIBLES - conv->desplazamiento);

                for (int k = 0; k < LINEAS_VISIBLES && inicio + k < total; k++) {
                    const Linea& l = conv->lineas[static_cast<size_t>(inicio + k)];
                    float y = AREA_Y + 6.0f + static_cast<float>(k) * ALTO_LINEA;

                    if (!l.avatar.empty()) {
                        c.avatares.dibujar(ventana, l.avatar, AREA_X + 8.0f, y + 1.0f, 22.0f);
                    }

                    ventana.dibujarTexto(l.principal, AREA_X + 10.0f + l.x, y, TAM_LETRA, l.colorPrincipal);

                    if (!l.secundario.empty()) {
                        ventana.dibujarTexto(l.secundario, AREA_X + 10.0f + l.xSecundario, y + 3.0f, 12, l.colorSecundario);
                    }

                    if (l.idMensaje != 0 && l.esMio && conexion.estaActiva()) { // "eliminar" (2 clics)
                        bool pendiente = (confirmarBorrado == l.idMensaje);
                        bool encima = mx >= BORRAR_X && mx < BORRAR_X + BORRAR_ANCHO && my >= y && my < y + ALTO_LINEA;

                        ventana.dibujarTexto(pendiente ? "¿seguro?" : "eliminar", BORRAR_X + 6.0f, y + 4.0f, 12,
                                             pendiente ? ROJO_ERROR : (encima ? ACENTO : sf::Color(205, 160, 190)));
                    }
                }

                if (total == 0) {
                    ventana.dibujarTexto("Aún no hay mensajes. ¡Escribe el primero!", AREA_X + 14, AREA_Y + 14, 15, TEXTO_SUAVE);
                }

                // barrita de scroll (solo si hay mas lineas de las que caben)
                if (total > LINEAS_VISIBLES) {
                    float proporcion = static_cast<float>(LINEAS_VISIBLES) / static_cast<float>(total);
                    float altoBarra = max(24.0f, (AREA_ALTO - 8.0f) * proporcion);
                    float recorrido = (AREA_ALTO - 8.0f) - altoBarra;
                    float avance = 1.0f - static_cast<float>(conv->desplazamiento) /
                                          static_cast<float>(total - LINEAS_VISIBLES);

                    rectangulo(ventana, AREA_X + AREA_ANCHO - 10.0f, AREA_Y + 4.0f + recorrido * avance,
                               6.0f, altoBarra, sf::Color(240, 175, 215));
                }

                if (conv->desplazamiento > 0) {
                    ventana.dibujarTexto("viendo mensajes anteriores  (End = ir al final)",
                                         AREA_X + 10.0f, AREA_Y + AREA_ALTO - 22.0f, 13, TEXTO_SUAVE);
                }

            } else if (conv != nullptr) { // canal al que aun no me uno
                ventana.dibujarTextoCentrado("Aún no te has unido a este canal.",
                                             sf::FloatRect(AREA_X, AREA_Y + AREA_ALTO / 2 - 40, AREA_ANCHO, 30),
                                             17, TEXTO_SUAVE);
                botonUnirse.actualizarHover(mx, my);
                botonUnirse.dibujar(ventana);

            } else {
                ventana.dibujarTextoCentrado("Elige una conversación en la lista de la izquierda.",
                                             sf::FloatRect(AREA_X, AREA_Y + AREA_ALTO / 2 - 15, AREA_ANCHO, 30),
                                             16, TEXTO_SUAVE);
            }

            if (!conexion.estaActiva()) {
                rectangulo(ventana, AREA_X, AREA_Y + AREA_ALTO - 28.0f, AREA_ANCHO, 28.0f, ROJO_ERROR);
                ventana.dibujarTextoCentrado("Desconectado del servidor. Cierra la ventana y vuelve a entrar.",
                                             sf::FloatRect(AREA_X, AREA_Y + AREA_ALTO - 28.0f, AREA_ANCHO, 28.0f),
                                             14, sf::Color::White);
            }

            // ---- caja de mensaje ----
            if (puedeEscribir(conv)) {
                campoMensaje.dibujar(ventana);
                botonEnviar.dibujar(ventana);
            } else {
                string motivo = "Elige una conversación para escribir";
                if (!conexion.estaActiva()) motivo = "Sin conexión con el servidor";
                else if (seccion == Seccion::CANALES && conv) motivo = conv->unido ? "En este canal solo escribe su dueño" : "Únete al canal para leerlo";

                rectangulo(ventana, AREA_X, 560, AREA_ANCHO, 42, sf::Color(255, 228, 245));
                ventana.dibujarTexto(motivo, AREA_X + 12, 570, 16, TEXTO_SUAVE);
            }

            if (!c.aviso.empty() && c.relojAviso.getElapsedTime().asSeconds() < 6.0f) {
                ventana.dibujarTexto(c.aviso, AREA_X, 612, 14, ROJO_ERROR);
            }

            // Dialogos modales: evitan eliminar algo con un clic no intencional.
            if (dialogo != Dialogo::NINGUNO) {
                rectangulo(ventana, 0, 0, ANCHO, ALTO, sf::Color(60, 20, 55, 165));
                if (dialogo == Dialogo::ADMIN_GRUPO) {
                    auto it = c.grupos.find(grupoAdministrado);
                    if (it != c.grupos.end()) {
                        const Conversacion& g = it->second;
                        bool dueno = (g.dueno == c.yo);
                        rectangulo(ventana, 230, 100, 580, 440, FONDO);
                        ventana.dibujarTexto("Administrar: " + g.titulo, 265, 120, 22, TEXTO_OSCURO);
                        ventana.dibujarTexto("Creador: " + g.dueno + "  ·  " + to_string(g.miembros.size()) + " miembros",
                                             265, 156, 15, TEXTO_SUAVE);
                        ventana.dibujarTexto(dueno ? "Retira miembros con Quitar (no puedes retirarte a ti)." :
                                             "Puedes ver los integrantes o salir de este grupo.", 265, 185, 13, TEXTO_SUAVE);
                        rectangulo(ventana, 258, 208, 518, 180, sf::Color::White);
                        scrollMiembros = max(0, min(scrollMiembros, max(0, static_cast<int>(g.miembros.size()) - 5)));
                        for (int j = 0; j < 5 && j + scrollMiembros < static_cast<int>(g.miembros.size()); ++j) {
                            const string& miembro = g.miembros[static_cast<size_t>(j + scrollMiembros)];
                            float y = 215.0f + j * 34.0f;
                            c.avatares.dibujar(ventana, miembro, 269, y, 26);
                            ventana.dibujarTexto(miembro + (miembro == g.dueno ? " (creador)" : ""),
                                                 307, y + 2, 16, TEXTO_OSCURO);
                            if (dueno && miembro != g.dueno) {
                                bool hover = mx >= 660 && mx < 780 && my >= y && my < y + 30;
                                rectangulo(ventana, 660, y, 112, 29, hover ? SELECCION : PANEL_LISTA);
                                ventana.dibujarTexto("Quitar", 686, y + 5, 14, ROJO_ERROR);
                            }
                        }
                        if (g.miembros.size() > 5)
                            ventana.dibujarTexto("Rueda del mouse para ver mas integrantes", 265, 394, 12, TEXTO_SUAVE);
                        if (dueno) {
                            campoNuevoMiembro.dibujar(ventana);
                            botonAgregarMiembro.actualizarHover(mx, my);
                            botonAgregarMiembro.dibujar(ventana);
                            botonEliminarGrupo.actualizarHover(mx, my);
                            botonEliminarGrupo.dibujar(ventana);
                        } else {
                            botonSalirDelGrupo.actualizarHover(mx, my);
                            botonSalirDelGrupo.dibujar(ventana);
                        }
                        botonCerrarDialogo.actualizarHover(mx, my);
                        botonCerrarDialogo.dibujar(ventana);
                    }
                } else if (dialogo == Dialogo::MIEMBROS_CANAL) {
                    auto it = c.canales.find(canalConsultado);
                    if (it != c.canales.end() && it->second.dueno == c.yo) {
                        const Conversacion& canal = it->second;
                        rectangulo(ventana, 230, 100, 580, 440, FONDO);
                        ventana.dibujarTexto("Miembros de: " + canal.titulo, 265, 120, 21, TEXTO_OSCURO);
                        ventana.dibujarTexto("Esta lista solo es visible para el creador.", 265, 157, 14, TEXTO_SUAVE);
                        const string estado = canal.miembrosCanalCargados ?
                            to_string(canal.miembros.size()) + " integrantes (incluyendote)" : "Consultando integrantes...";
                        ventana.dibujarTexto(estado, 265, 182, 14, TEXTO_OSCURO);
                        rectangulo(ventana, 258, 211, 518, 245, sf::Color::White);
                        scrollMiembrosCanal = max(0, min(scrollMiembrosCanal,
                            max(0, static_cast<int>(canal.miembros.size()) - 7)));
                        for (int j = 0; j < 7 && j + scrollMiembrosCanal < static_cast<int>(canal.miembros.size()); ++j) {
                            const string& u = canal.miembros[static_cast<size_t>(j + scrollMiembrosCanal)];
                            float y = 218.0f + j * 33.0f;
                            c.avatares.dibujar(ventana, u, 269, y, 26);
                            ventana.dibujarTexto(u + (u == canal.dueno ? " (creador)" : ""),
                                                 308, y + 2, 16, TEXTO_OSCURO);
                        }
                        if (canal.miembrosCanalCargados && canal.miembros.size() > 7)
                            ventana.dibujarTexto("Rueda del mouse para ver más integrantes", 265, 462, 12, TEXTO_SUAVE);
                        botonCerrarDialogo.actualizarHover(mx, my);
                        botonCerrarDialogo.dibujar(ventana);
                    }
                } else if (dialogo == Dialogo::CONFIRMAR) {
                    rectangulo(ventana, 260, 210, 480, 240, FONDO);
                    string titulo, detalle;
                    switch (accionConfirmar) {
                        case AccionConfirmar::QUITAR_MIEMBRO:
                            titulo = "Quitar a " + usuarioConfirmar + " del grupo?";
                            detalle = "Dejara de ver los mensajes de ese grupo.";
                            break;
                        case AccionConfirmar::SALIR_GRUPO:
                            titulo = "Abandonar el grupo?";
                            detalle = "Ya no podras ver sus conversaciones.";
                            break;
                        case AccionConfirmar::ELIMINAR_GRUPO:
                            titulo = "Eliminar este grupo definitivamente?";
                            detalle = "Se borraran sus mensajes y lista de integrantes.";
                            break;
                        case AccionConfirmar::ELIMINAR_CANAL:
                            titulo = "Eliminar este canal definitivamente?";
                            detalle = "Se borraran sus mensajes y suscripciones.";
                            break;
                        default: break;
                    }
                    ventana.dibujarTexto(titulo, 280, 248, 19, TEXTO_OSCURO);
                    ventana.dibujarTexto(detalle, 280, 291, 15, ROJO_ERROR);
                    botonConfirmarSi.actualizarHover(mx, my);
                    botonConfirmarNo.actualizarHover(mx, my);
                    botonConfirmarSi.dibujar(ventana);
                    botonConfirmarNo.dibujar(ventana);
                } else if (dialogo == Dialogo::CUENTA) {
                    rectangulo(ventana, 250, 165, 500, 335, FONDO);
                    ventana.dibujarTexto("Eliminar mi cuenta", 305, 190, 25, ROJO_ERROR);
                    ventana.dibujarTexto("Esta accion es irreversible: se eliminaran", 305, 235, 15, TEXTO_OSCURO);
                    ventana.dibujarTexto("tus mensajes, grupos y canales creados.", 305, 258, 15, TEXTO_OSCURO);
                    // CampoTexto ya dibuja su propia etiqueta (24 px sobre la caja).
                    campoClaveEliminar.dibujar(ventana);
                    botonConfirmarCuenta.setTexto(confirmarCuenta ? "Si, eliminar definitivamente" : "Eliminar mi cuenta");
                    botonConfirmarCuenta.actualizarHover(mx, my);
                    botonCancelarCuenta.actualizarHover(mx, my);
                    botonConfirmarCuenta.dibujar(ventana);
                    botonCancelarCuenta.dibujar(ventana);
                }
            }
        }

        win.display();
    }

    // =================== CIERRE ORDENADO ===================
    if (conexion.estaActiva()) {
        conexion.enviar("SALIR");
        conexion.actualizar(); // intenta mandar el SALIR antes de cerrar
    }

    conexion.cerrar();
    return 0;
}
