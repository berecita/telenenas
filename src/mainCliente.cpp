// =====================================================================
//  CHAT - Cliente grafico (SFML)
//
//  Misma filosofia que mainCliente.cpp de upifood: la ventana es la
//  interfaz y todo vive dentro de UN SOLO CICLO (eventos -> actualizar ->
//  dibujar). Nunca hay cin ni esperas largas con la ventana abierta.
//
//  Lo NUEVO respecto a upifood: en upifood el cliente preguntaba y el
//  servidor contestaba (peticion/respuesta). En un chat el servidor puede
//  mandar mensajes EN CUALQUIER MOMENTO, asi que hace falta un HILO
//  RECEPTOR que se queda en recv() y deja lo que llega en una cola. El
//  ciclo de la ventana vacia esa cola en cada cuadro. Asi la ventana
//  nunca se congela esperando mensajes.
//
//      [hilo receptor] --recv()--> cola (con mutex) --> [ciclo SFML] --> pantalla
//      [ciclo SFML] --send()--> servidor        (enviar no bloquea: son lineas cortas)
//
//  Pantallas:  LOGIN (IP, puerto, nombre)  ->  CHAT
//  Uso:  ./cliente [ip] [puerto]
// =====================================================================

#include "Protocolo.h"
#include "Ventana.h"
#include "Widgets.h"
#include "Config.h"

#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace std;

enum class Pantalla { LOGIN, CHAT };

// ---- Colores (los mismos de upifood) ----
static const sf::Color FONDO(245, 241, 233);
static const sf::Color BARRA_LATERAL(74, 50, 35);
static const sf::Color CREMA(250, 246, 238);
static const sf::Color TEXTO_OSCURO(50, 40, 35);
static const sf::Color TEXTO_SUAVE(130, 120, 110);
static const sf::Color ROJO_ERROR(180, 40, 40);
static const sf::Color VERDE(40, 160, 90);
static const sf::Color AZUL(60, 110, 180);
static const sf::Color MORADO(130, 60, 160);

// ---- Medidas ----
static const int ANCHO = 900;
static const int ALTO = 600;
static const float AREA_X = 215.0f;       // zona donde se ven los mensajes
static const float AREA_Y = 60.0f;
static const float AREA_ANCHO = 665.0f;
static const float AREA_ALTO = 430.0f;
static const unsigned int TAM_LETRA = 16;
static const float ALTO_LINEA = 22.0f;
static const int LINEAS_VISIBLES = static_cast<int>(AREA_ALTO / ALTO_LINEA); // 19
static const float ANCHO_TEXTO = AREA_ANCHO - 34.0f; // deja margen y espacio para la barra de scroll
static const float SANGRIA = 22.0f;       // las lineas que continuan van un poco metidas
static const size_t MAX_LINEAS = 1000;    // historial maximo en pantalla

// Una linea YA ajustada al ancho de la zona (un mensaje largo ocupa varias).
struct Linea {
    string prefijo;        // "[12:30] ana: " (solo en la primera linea del mensaje)
    sf::Color colorPrefijo;
    string texto;
    sf::Color colorTexto;
    float xTexto;          // desde donde empieza el texto, relativo al borde de la zona
};

// ---------------------------------------------------------------------
// Comunicacion entre el hilo receptor y el ciclo de la ventana
// ---------------------------------------------------------------------
static mutex mutexCola;
static deque<string> cola;          // lineas recibidas, aun sin mostrar
static atomic<bool> conectado{false};

static void hiloReceptor(int fd) {
    string linea;

    while (recibirMensaje(fd, linea)) {
        lock_guard<mutex> g(mutexCola);
        cola.push_back(linea);
    }

    conectado = false; // recv() fallo: el servidor cerro o se cayo la red

    lock_guard<mutex> g(mutexCola);
    cola.push_back("SYS|Se perdio la conexion con el servidor.");
}

// ---------------------------------------------------------------------
// Utilidades de texto
// ---------------------------------------------------------------------
static string quitarEspacios(const string& s) {
    size_t i = 0, j = s.size();
    while (i < j && isspace(static_cast<unsigned char>(s[i]))) i++;
    while (j > i && isspace(static_cast<unsigned char>(s[j - 1]))) j--;
    return s.substr(i, j - i);
}

// Posicion del siguiente caracter UTF-8 (las ñ y acentos ocupan 2 bytes).
static size_t siguienteCaracter(const string& s, size_t i) {
    i++;
    while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) {
        i++;
    }
    return i;
}

// Parte un texto en varias lineas para que ninguna pase de cierto ancho en
// pixeles. Corta por palabras; si una palabra sola no cabe, la corta por
// caracteres. La primera linea puede tener menos espacio (porque antes va
// el "[hora] usuario:").
static vector<string> ajustar(Ventana& v, const string& texto, unsigned int size,
                              float anchoPrimera, float anchoResto) {
    vector<string> lineas;
    string actual;

    auto ancho = [&]() { return lineas.empty() ? anchoPrimera : anchoResto; };

    size_t i = 0;
    while (i < texto.size()) {
        size_t fin = texto.find(' ', i);
        if (fin == string::npos) fin = texto.size();

        string palabra = texto.substr(i, fin - i);
        i = fin + 1;

        if (palabra.empty()) continue; // espacios repetidos

        string candidato = actual.empty() ? palabra : actual + " " + palabra;

        if (v.medirTexto(candidato, size) <= ancho()) {
            actual = candidato;
            continue;
        }

        if (!actual.empty()) {
            lineas.push_back(actual);
            actual.clear();
        }

        // Palabra mas ancha que una linea completa: partirla por caracteres.
        while (v.medirTexto(palabra, size) > ancho()) {
            size_t corte = siguienteCaracter(palabra, 0);

            while (corte < palabra.size()) {
                size_t sig = siguienteCaracter(palabra, corte);
                if (v.medirTexto(palabra.substr(0, sig), size) > ancho()) break;
                corte = sig;
            }

            lineas.push_back(palabra.substr(0, corte));
            palabra = palabra.substr(corte);
        }

        actual = palabra;
    }

    if (!actual.empty() || lineas.empty()) {
        lineas.push_back(actual);
    }

    return lineas;
}

// ---------------------------------------------------------------------
// Manejo de varios campos de texto (foco, Tab) - igual que en upifood
// ---------------------------------------------------------------------
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

static void rectangulo(Ventana& v, float x, float y, float ancho, float alto, sf::Color color) {
    sf::RectangleShape r(sf::Vector2f(ancho, alto));
    r.setPosition(x, y);
    r.setFillColor(color);
    v.getWindow().draw(r);
}

// =====================================================================
int main(int argc, char* argv[]) {
    Ventana ventana("Chat - Sockets y SFML", ANCHO, ALTO);
    ventana.abrir();
    sf::RenderWindow& win = ventana.getWindow();

    // ---------------- Estado de la conexion ----------------
    int socketFd = -1;
    thread receptor;
    string miNombre;
    Pantalla pantalla = Pantalla::LOGIN;

    // ---------------- LOGIN ----------------
    CampoTexto campoIp("Servidor (IP o nombre)", 300, 170, 300, 42);
    CampoTexto campoPuerto("Puerto", 300, 250, 300, 42);
    CampoTexto campoUsuario("Nombre de usuario (máx. 16)", 300, 330, 300, 42);
    Boton botonEntrar("Entrar al chat", 300, 400, 300, 46);
    campoIp.setMaxLongitud(60);
    campoIp.setContenido(resolverIp(argc, argv));
    campoPuerto.setMaxLongitud(5);
    campoPuerto.setModo(ModoCampo::SOLO_DIGITOS);
    campoPuerto.setContenido(to_string(resolverPuerto(argc, argv)));
    campoUsuario.setMaxLongitud(16);
    vector<CampoTexto*> camposLogin = {&campoIp, &campoPuerto, &campoUsuario};
    campoUsuario.setEnfocado(true);
    string mensajeLogin;

    // ---------------- CHAT ----------------
    CampoTexto campoMensaje("Mensaje      (/w usuario texto = mensaje privado)", AREA_X, 530, 500, 42);
    campoMensaje.setMaxLongitud(400);
    campoMensaje.setEnfocado(true);
    Boton botonEnviar("Enviar", 725, 530, 155, 42);
    Boton botonSalir("Salir", 20, 535, 160, 44);
    botonSalir.setColor(sf::Color(150, 130, 112));

    vector<Linea> lineas;          // todo el historial, ya ajustado
    vector<string> usuarios;       // quienes estan en linea
    int desplazamiento = 0;        // cuantas lineas estoy "arriba" del final (0 = viendo lo ultimo)

    // Agrega un mensaje al historial (lo parte en varias lineas si es largo).
    auto agregarLinea = [&](const string& prefijo, sf::Color colorPrefijo,
                            const string& texto, sf::Color colorTexto) {
        float anchoPrefijo = ventana.medirTexto(prefijo, TAM_LETRA);
        vector<string> partes = ajustar(ventana, texto, TAM_LETRA,
                                        ANCHO_TEXTO - anchoPrefijo, ANCHO_TEXTO - SANGRIA);

        for (size_t i = 0; i < partes.size(); i++) {
            Linea l;
            l.prefijo = (i == 0) ? prefijo : string();
            l.colorPrefijo = colorPrefijo;
            l.texto = partes[i];
            l.colorTexto = colorTexto;
            l.xTexto = (i == 0) ? anchoPrefijo : SANGRIA;
            lineas.push_back(l);
        }

        // Si estoy leyendo mensajes viejos, que la vista no salte con los nuevos.
        if (desplazamiento > 0) {
            desplazamiento += static_cast<int>(partes.size());
        }

        while (lineas.size() > MAX_LINEAS) {
            lineas.erase(lineas.begin());
        }
    };

    auto avisoLocal = [&](const string& texto, sf::Color color) {
        agregarLinea("", color, "* " + texto, color);
    };

    // Interpreta UNA linea que mando el servidor.
    auto procesarLinea = [&](const string& linea) {
        vector<string> c = separarMaximo(linea, 4);
        const string& cmd = c[0];

        if (cmd == "MSG" && c.size() >= 4) {
            bool mio = (c[2] == miNombre);
            agregarLinea("[" + c[1] + "] " + c[2] + ": ", mio ? VERDE : AZUL, c[3], TEXTO_OSCURO);

        } else if (cmd == "PRIV" && c.size() >= 4) {
            agregarLinea("[" + c[1] + "] (privado) " + c[2] + ": ", MORADO, c[3], MORADO);

        } else if (cmd == "PRIVOUT" && c.size() >= 4) {
            agregarLinea("[" + c[1] + "] (privado para " + c[2] + "): ", MORADO, c[3], MORADO);

        } else if (cmd == "SYS" && linea.size() > 4) {
            avisoLocal(linea.substr(4), TEXTO_SUAVE);

        } else if (cmd == "USERS") {
            usuarios.clear();
            string lista = (linea.size() > 6) ? linea.substr(6) : "";
            size_t inicio = 0;

            while (inicio <= lista.size() && !lista.empty()) {
                size_t coma = lista.find(',', inicio);
                if (coma == string::npos) coma = lista.size();
                usuarios.push_back(lista.substr(inicio, coma - inicio));
                inicio = coma + 1;
            }
        }
    };

    // Valida el login, conecta, manda el nombre y arranca el hilo receptor.
    auto intentarEntrar = [&]() {
        string ip = quitarEspacios(campoIp.getContenido());
        string textoPuerto = campoPuerto.getContenido();
        string nombre = quitarEspacios(campoUsuario.getContenido());
        int puerto = textoPuerto.empty() ? 0 : atoi(textoPuerto.c_str());

        if (ip.empty()) { mensajeLogin = "Escribe la IP del servidor."; return; }
        if (puerto <= 0 || puerto > 65535) { mensajeLogin = "Puerto no válido (1-65535)."; return; }
        if (nombre.empty()) { mensajeLogin = "Escribe un nombre de usuario."; return; }
        if (nombre.find('|') != string::npos || nombre.find(',') != string::npos) {
            mensajeLogin = "El nombre no puede llevar '|' ni ','.";
            return;
        }

        string error;
        int fd = conectarConTimeout(ip, puerto, 3000, error);

        if (fd == -1) {
            mensajeLogin = error;
            return;
        }

        enviarMensaje(fd, "NICK|" + nombre);
        ponerTimeoutRecepcion(fd, 3000); // no esperar la respuesta para siempre

        string respuesta;

        if (!recibirMensaje(fd, respuesta)) {
            mensajeLogin = "El servidor no respondió.";
            close(fd);
            return;
        }

        if (respuesta.rfind("OK", 0) != 0) {
            vector<string> c = separarMaximo(respuesta, 2);
            mensajeLogin = (c.size() > 1) ? c[1] : "El servidor rechazó la conexión.";
            close(fd);
            return;
        }

        ponerTimeoutRecepcion(fd, 0); // el hilo receptor SI espera sin limite

        socketFd = fd;
        miNombre = nombre;
        conectado = true;
        receptor = thread(hiloReceptor, fd);

        pantalla = Pantalla::CHAT;
        avisoLocal("Conectado a " + ip + ":" + to_string(puerto) + " como " + nombre + ".", TEXTO_SUAVE);
        avisoLocal("Escribe /w usuario mensaje para hablarle en privado.", TEXTO_SUAVE);
    };

    // Lee la caja de texto y manda el mensaje.
    auto enviarTexto = [&]() {
        string t = quitarEspacios(campoMensaje.getContenido());

        if (t.empty()) return;

        if (!conectado) {
            return; // el aviso rojo de abajo ya dice que no hay conexion
        }

        if (t.rfind("/w ", 0) == 0) {
            string resto = quitarEspacios(t.substr(3));
            size_t espacio = resto.find(' ');

            if (espacio == string::npos) {
                avisoLocal("Uso: /w usuario mensaje", ROJO_ERROR);
            } else {
                enviarMensaje(socketFd, "PRIV|" + resto.substr(0, espacio) + "|" +
                                        quitarEspacios(resto.substr(espacio + 1)));
            }
        } else if (t[0] == '/') {
            avisoLocal("Comando desconocido. Usa /w usuario mensaje para un privado.", ROJO_ERROR);
        } else {
            enviarMensaje(socketFd, "MSG|" + t);
        }

        campoMensaje.limpiar();
        desplazamiento = 0; // al mandar, regresar a lo mas reciente
    };

    // =================== CICLO PRINCIPAL ===================
    while (win.isOpen()) {

        // ---- 1) EVENTOS ----
        sf::Event e;

        while (win.pollEvent(e)) {
            if (e.type == sf::Event::Closed) {
                win.close();
                continue;
            }

            if (pantalla == Pantalla::LOGIN) {
                if (e.type == sf::Event::TextEntered) {
                    teclearEnCampos(camposLogin, e.text.unicode);

                } else if (e.type == sf::Event::KeyPressed) {
                    if (e.key.code == sf::Keyboard::Tab) siguienteCampo(camposLogin);
                    if (e.key.code == sf::Keyboard::Return) intentarEntrar();

                } else if (e.type == sf::Event::MouseButtonPressed &&
                           e.mouseButton.button == sf::Mouse::Left) {
                    float mx = static_cast<float>(e.mouseButton.x);
                    float my = static_cast<float>(e.mouseButton.y);

                    clicEnCampos(camposLogin, mx, my);
                    if (botonEntrar.contiene(mx, my)) intentarEntrar();
                }

            } else { // CHAT
                if (e.type == sf::Event::TextEntered) {
                    campoMensaje.recibirCaracter(e.text.unicode);

                } else if (e.type == sf::Event::KeyPressed) {
                    switch (e.key.code) {
                        case sf::Keyboard::Return:   enviarTexto(); break;
                        case sf::Keyboard::PageUp:   desplazamiento += LINEAS_VISIBLES - 1; break;
                        case sf::Keyboard::PageDown: desplazamiento -= LINEAS_VISIBLES - 1; break;
                        case sf::Keyboard::Up:       desplazamiento += 1; break;
                        case sf::Keyboard::Down:     desplazamiento -= 1; break;
                        case sf::Keyboard::End:      desplazamiento = 0; break;
                        default: break;
                    }

                } else if (e.type == sf::Event::MouseWheelScrolled) {
                    desplazamiento += static_cast<int>(e.mouseWheelScroll.delta * 3.0f);

                } else if (e.type == sf::Event::MouseButtonPressed &&
                           e.mouseButton.button == sf::Mouse::Left) {
                    float mx = static_cast<float>(e.mouseButton.x);
                    float my = static_cast<float>(e.mouseButton.y);

                    if (botonEnviar.contiene(mx, my)) enviarTexto();
                    if (botonSalir.contiene(mx, my)) win.close();
                }
            }
        }

        // ---- 2) ACTUALIZAR: vaciar la cola que lleno el hilo receptor ----
        if (pantalla == Pantalla::CHAT) {
            vector<string> pendientes;

            {
                lock_guard<mutex> g(mutexCola); // seccion critica cortita: solo mover los datos
                while (!cola.empty()) {
                    pendientes.push_back(cola.front());
                    cola.pop_front();
                }
            }

            for (const string& linea : pendientes) {
                procesarLinea(linea);
            }

            int maxDesplazamiento = max(0, static_cast<int>(lineas.size()) - LINEAS_VISIBLES);
            desplazamiento = max(0, min(desplazamiento, maxDesplazamiento));
        }

        sf::Vector2i mouse = sf::Mouse::getPosition(win);
        float mx = static_cast<float>(mouse.x);
        float my = static_cast<float>(mouse.y);

        // ---- 3) DIBUJAR ----
        win.clear(FONDO);

        if (pantalla == Pantalla::LOGIN) {
            botonEntrar.actualizarHover(mx, my);

            ventana.dibujarTexto("Chat con sockets y SFML", 255, 60, 30, TEXTO_OSCURO);
            ventana.dibujarTexto("Conéctate al servidor para entrar a la sala", 262, 105, 16, TEXTO_SUAVE);

            campoIp.dibujar(ventana);
            campoPuerto.dibujar(ventana);
            campoUsuario.dibujar(ventana);
            botonEntrar.dibujar(ventana);

            ventana.dibujarTexto(mensajeLogin, 300, 460, 16, ROJO_ERROR);
            ventana.dibujarTexto("Tab: cambiar de campo    Enter: entrar", 300, 560, 14, TEXTO_SUAVE);

        } else {
            botonEnviar.actualizarHover(mx, my);
            botonSalir.actualizarHover(mx, my);

            // -- barra lateral --
            rectangulo(ventana, 0, 0, 200, ALTO, BARRA_LATERAL);
            ventana.dibujarTexto("CHAT", 20, 20, 28, sf::Color::White);
            ventana.dibujarTexto("En línea (" + to_string(usuarios.size()) + ")", 20, 90, 16, sf::Color(210, 190, 170));

            for (size_t i = 0; i < usuarios.size() && i < 15; i++) {
                string nombre = usuarios[i] + (usuarios[i] == miNombre ? "  (tú)" : "");
                ventana.dibujarTexto("• " + nombre, 20, 125.0f + static_cast<float>(i) * 26.0f, 16, CREMA);
            }

            botonSalir.dibujar(ventana);

            // -- encabezado --
            ventana.dibujarTexto("Sala general", AREA_X, 10, 24, TEXTO_OSCURO);
            ventana.dibujarTexto("Conectado como " + miNombre, AREA_X, 38, 14, TEXTO_SUAVE);

            // -- zona de mensajes --
            sf::RectangleShape zona(sf::Vector2f(AREA_ANCHO, AREA_ALTO));
            zona.setPosition(AREA_X, AREA_Y);
            zona.setFillColor(sf::Color::White);
            zona.setOutlineThickness(2.0f);
            zona.setOutlineColor(sf::Color(190, 180, 170));
            win.draw(zona);

            const int total = static_cast<int>(lineas.size());
            const int inicio = max(0, total - LINEAS_VISIBLES - desplazamiento);

            for (int k = 0; k < LINEAS_VISIBLES && inicio + k < total; k++) {
                const Linea& l = lineas[static_cast<size_t>(inicio + k)];
                float y = AREA_Y + 6.0f + static_cast<float>(k) * ALTO_LINEA;

                if (!l.prefijo.empty()) {
                    ventana.dibujarTexto(l.prefijo, AREA_X + 10.0f, y, TAM_LETRA, l.colorPrefijo);
                }

                ventana.dibujarTexto(l.texto, AREA_X + 10.0f + l.xTexto, y, TAM_LETRA, l.colorTexto);
            }

            // -- barrita de scroll (solo si hay mas lineas de las que caben) --
            if (total > LINEAS_VISIBLES) {
                float proporcion = static_cast<float>(LINEAS_VISIBLES) / static_cast<float>(total);
                float altoBarra = max(24.0f, (AREA_ALTO - 8.0f) * proporcion);
                float recorrido = (AREA_ALTO - 8.0f) - altoBarra;
                float avance = 1.0f - static_cast<float>(desplazamiento) /
                                      static_cast<float>(total - LINEAS_VISIBLES);

                rectangulo(ventana, AREA_X + AREA_ANCHO - 10.0f, AREA_Y + 4.0f + recorrido * avance,
                           6.0f, altoBarra, sf::Color(200, 190, 178));
            }

            if (desplazamiento > 0) {
                ventana.dibujarTexto("viendo mensajes anteriores  (End = ir al final)",
                                     AREA_X + 10.0f, AREA_Y + AREA_ALTO - 22.0f, 13, TEXTO_SUAVE);
            }

            if (!conectado) {
                rectangulo(ventana, AREA_X, AREA_Y + AREA_ALTO - 28.0f, AREA_ANCHO, 28.0f, ROJO_ERROR);
                ventana.dibujarTextoCentrado("Desconectado del servidor. Cierra la ventana y vuelve a entrar.",
                                             sf::FloatRect(AREA_X, AREA_Y + AREA_ALTO - 28.0f, AREA_ANCHO, 28.0f),
                                             15, sf::Color::White);
            }

            campoMensaje.dibujar(ventana);
            botonEnviar.dibujar(ventana);
        }

        win.display();
    }

    // =================== CIERRE ORDENADO ===================
    if (socketFd != -1) {
        enviarMensaje(socketFd, "SALIR");
        shutdown(socketFd, SHUT_RDWR); // hace que recv() del hilo receptor regrese
    }

    if (receptor.joinable()) {
        receptor.join();
    }

    if (socketFd != -1) {
        close(socketFd);
    }

    return 0;
}

