// =====================================================================
//  CHAT - Cliente grafico (SFML)
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
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace std;

enum class Pantalla { LOGIN, CHAT };
enum class Fase { EDITANDO, CONECTANDO, ESPERANDO };
enum class Seccion { GENERAL, DIRECTOS, GRUPOS, CANALES };

// ---- Colores (los mismos de upifood) ----
static const sf::Color FONDO(245, 241, 233);
static const sf::Color BARRA_LATERAL(74, 50, 35);
static const sf::Color PANEL_LISTA(236, 229, 218);
static const sf::Color TEXTO_OSCURO(50, 40, 35);
static const sf::Color TEXTO_SUAVE(130, 120, 110);
static const sf::Color ROJO_ERROR(180, 40, 40);
static const sf::Color VERDE(40, 160, 90);
static const sf::Color SELECCION(214, 196, 172);
static const sf::Color NARANJA(220, 120, 30);

// ---- Medidas ----
static const int ANCHO = 1000;
static const int ALTO = 640;
static const float LISTA_X = 200.0f;
static const float LISTA_ANCHO = 210.0f;
static const float LISTA_Y = 70.0f;
static const float FILA_ALTO = 44.0f;

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
    Ventana ventana("Chat - Sockets y SFML", ANCHO, ALTO);
    ventana.abrir();
    sf::RenderWindow& win = ventana.getWindow();

    Conexion conexion;
    unique_ptr<Chat> chat;
    Pantalla pantalla = Pantalla::LOGIN;

    // =============== LOGIN ===============
    CampoTexto campoIp("Servidor (IP o nombre)", 350, 140, 300, 42);
    CampoTexto campoPuerto("Puerto", 350, 205, 300, 42);
    CampoTexto campoUsuario("Usuario (máx. 16)", 350, 270, 300, 42);
    CampoTexto campoClave("Contraseña (mín. 4)", 350, 335, 300, 42, true);
    CampoTexto campoFoto("Foto de perfil: ruta de una imagen (opcional)", 350, 400, 300, 42);
    Boton botonEntrar("Entrar", 350, 455, 145, 46);
    Boton botonRegistro("Registrarme", 505, 455, 145, 46);

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

    CampoTexto campoMensaje("Mensaje", AREA_X, 560, 400, 42);
    campoMensaje.setMaxLongitud(MAX_TEXTO);
    CampoTexto campoNombreGrupo("Nombre del grupo", LISTA_X, 506, LISTA_ANCHO, 38);
    campoNombreGrupo.setMaxLongitud(MAX_NOMBRE_SALA);
    CampoTexto campoNombreCanal("Nombre del canal nuevo", LISTA_X, 506, LISTA_ANCHO, 38);
    campoNombreCanal.setMaxLongitud(MAX_NOMBRE_SALA);
    vector<CampoTexto*> camposChat = {&campoMensaje, &campoNombreGrupo, &campoNombreCanal};

    Boton botonEnviar("Enviar", 835, 560, 150, 42);
    Boton botonSalir("Salir", 15, 575, 160, 44);
    botonSalir.setColor(sf::Color(150, 130, 112));
    Boton botonSeccion[4] = {Boton("General", 15, 100, 160, 44), Boton("Directos", 15, 152, 160, 44),
                             Boton("Grupos", 15, 204, 160, 44), Boton("Canales", 15, 256, 160, 44)};
    Boton botonNuevoGrupo("Nuevo grupo", LISTA_X, 560, LISTA_ANCHO, 44);
    Boton botonCrearGrupo("Crear grupo", LISTA_X, 556, LISTA_ANCHO, 40);
    Boton botonCancelar("Cancelar", LISTA_X, 602, LISTA_ANCHO, 30);
    botonCancelar.setColor(sf::Color(150, 130, 112));
    Boton botonCrearCanal("Crear canal", LISTA_X, 556, LISTA_ANCHO, 44);
    Boton botonUnirse("Unirme al canal", AREA_X + AREA_ANCHO / 2 - 100, AREA_Y + AREA_ALTO / 2 + 10, 200, 46);

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

            if (!rutaFoto.empty() && !prepararFoto(rutaFoto, intento.foto, error)) {
                mostrarError(error);
                return;
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
            chat = make_unique<Chat>(ventana);
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

    auto cambiarSeccion = [&](Seccion nueva) {
        seccion = nueva;
        modoGrupo = false;
        marcados.clear();
        scrollLista = 0;
        enfocar(camposChat, &campoMensaje);
    };

    // Campos de texto que estan en pantalla ahora mismo (para Tab, clics y teclas).
    auto camposVisibles = [&]() {
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
                    if (botonEntrar.contiene(mx, my)) iniciarIntento(false);
                    if (botonRegistro.contiene(mx, my)) iniciarIntento(true);
                }
                continue;
            }

            // ----- pantalla de CHAT -----
            vector<CampoTexto*> visibles = camposVisibles();
            Conversacion* conv = conversacionActual();

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

                // -- barra lateral --
                for (int i = 0; i < 4; i++) {
                    if (botonSeccion[i].contiene(mx, my)) cambiarSeccion(static_cast<Seccion>(i));
                }
                if (botonSalir.contiene(mx, my)) win.close();

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
            } else {
                chat->procesar(linea);
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

            ventana.dibujarTexto("Chat con sockets y SFML", 330, 38, 32, TEXTO_OSCURO);
            ventana.dibujarTexto("Entra con tu cuenta o créate una (con foto, si quieres)", 306, 88, 16, TEXTO_SUAVE);

            for (CampoTexto* c : camposLogin) c->dibujar(ventana);
            botonEntrar.dibujar(ventana);
            botonRegistro.dibujar(ventana);

            ventana.dibujarTexto(mensajeLogin, 350, 520, 16, mensajeEsError ? ROJO_ERROR : TEXTO_SUAVE);
            ventana.dibujarTexto("Tab: cambiar de campo    Enter: entrar    Ctrl+V: pegar", 350, 592, 13, TEXTO_SUAVE);
            ventana.dibujarTexto("La foto solo se usa al registrarte (png, jpg, bmp o tga).", 350, 610, 13, TEXTO_SUAVE);

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

            // ---- barra lateral ----
            rectangulo(ventana, 0, 0, 190, ALTO, BARRA_LATERAL);
            c.avatares.dibujar(ventana, c.yo, 15, 18, 44);
            ventana.dibujarTexto(c.yo, 70, 20, 16, sf::Color::White);
            ventana.dibujarTexto(conexion.estaActiva() ? "en línea" : "sin conexión", 70, 44, 13,
                                 conexion.estaActiva() ? sf::Color(150, 220, 170) : sf::Color(240, 150, 150));
            for (int i = 0; i < 4; i++) botonSeccion[i].dibujar(ventana);
            botonSalir.dibujar(ventana);

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
                               seleccionado ? SELECCION : sf::Color(225, 215, 200));
                }

                float xTexto = LISTA_X + 4;

                if (it.conCheck) {
                    rectangulo(ventana, LISTA_X, y + 12, 18, 18, sf::Color(120, 100, 85));
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
                    punto.setFillColor(NARANJA);
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

            ventana.dibujarTexto(titulo, AREA_X, 14, 24, TEXTO_OSCURO);
            ventana.dibujarTexto(subtitulo, AREA_X, 44, 13, TEXTO_SUAVE);

            // ---- zona de mensajes ----
            sf::RectangleShape zona(sf::Vector2f(AREA_ANCHO, AREA_ALTO));
            zona.setPosition(AREA_X, AREA_Y);
            zona.setFillColor(sf::Color::White);
            zona.setOutlineThickness(2.0f);
            zona.setOutlineColor(sf::Color(190, 180, 170));
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
                               6.0f, altoBarra, sf::Color(200, 190, 178));
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

                rectangulo(ventana, AREA_X, 560, AREA_ANCHO, 42, sf::Color(228, 221, 210));
                ventana.dibujarTexto(motivo, AREA_X + 12, 570, 16, TEXTO_SUAVE);
            }

            if (!c.aviso.empty() && c.relojAviso.getElapsedTime().asSeconds() < 6.0f) {
                ventana.dibujarTexto(c.aviso, AREA_X, 612, 14, ROJO_ERROR);
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

