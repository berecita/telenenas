#include "Chat.h"

#include "../comun/Protocolo.h"

using namespace std;

static const sf::Color TEXTO_OSCURO(74, 30, 64);
static const sf::Color TEXTO_SUAVE(150, 100, 138);
static const sf::Color NOMBRE_MIO(214, 60, 150);
static const sf::Color NOMBRE_OTRO(112, 82, 190);

static const float SANGRIA = 32.0f;                              // espacio del avatar
static const float ANCHO_TEXTO = AREA_ANCHO - 10.0f - SANGRIA - 18.0f; // margen derecho y barra de scroll
static const size_t MAX_MENSAJES = 600;                          // historial maximo por conversacion

// ---------------------------------------------------------------------
// Ajuste de texto
// ---------------------------------------------------------------------

// Posicion del siguiente caracter UTF-8 (las ñ y acentos ocupan 2 bytes).
static size_t siguienteCaracter(const string& s, size_t i) {
    i++;
    while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) {
        i++;
    }
    return i;
}

// Parte un texto en varias lineas para que ninguna pase de `ancho` pixeles.
// Corta por palabras; si una palabra sola no cabe, la corta por caracteres.
static vector<string> ajustar(Ventana& v, const string& texto, unsigned int size, float ancho) {
    vector<string> lineas;
    string actual;
    size_t i = 0;

    while (i < texto.size()) {
        size_t fin = texto.find(' ', i);
        if (fin == string::npos) fin = texto.size();

        string palabra = texto.substr(i, fin - i);
        i = fin + 1;

        if (palabra.empty()) continue; // espacios repetidos

        string candidato = actual.empty() ? palabra : actual + " " + palabra;

        if (v.medirTexto(candidato, size) <= ancho) {
            actual = candidato;
            continue;
        }

        if (!actual.empty()) {
            lineas.push_back(actual);
            actual.clear();
        }

        // Palabra mas ancha que una linea completa: partirla por caracteres.
        while (v.medirTexto(palabra, size) > ancho) {
            size_t corte = siguienteCaracter(palabra, 0);

            while (corte < palabra.size()) {
                size_t sig = siguienteCaracter(palabra, corte);
                if (v.medirTexto(palabra.substr(0, sig), size) > ancho) break;
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

void Chat::verUsuario(const string& usuario) {
    if (!usuario.empty() && avatares.necesitaPedir(usuario)) {
        porPedir.push_back(usuario);
    }
}

vector<string> Chat::fotosPorPedir() {
    vector<string> lista;
    lista.swap(porPedir);
    return lista;
}

Conversacion& Chat::directo(const string& usuario) {
    auto it = directos.find(usuario);

    if (it == directos.end()) {
        Conversacion c;
        c.id = usuario;
        c.titulo = usuario;
        it = directos.emplace(usuario, c).first;
    }

    verUsuario(usuario);
    return it->second;
}

// Convierte un mensaje en lineas ya ajustadas al ancho de la zona.
void Chat::agregarLineas(Conversacion& conv, const MensajeVista& m) {
    bool mio = (m.autor == yo);
    float anchoNombre = ventana.medirTexto(m.autor, TAM_LETRA);

    Linea encabezado;
    encabezado.principal = m.autor;
    encabezado.colorPrincipal = mio ? NOMBRE_MIO : NOMBRE_OTRO;
    encabezado.secundario = m.hora;
    encabezado.colorSecundario = TEXTO_SUAVE;
    encabezado.x = SANGRIA;
    encabezado.xSecundario = SANGRIA + anchoNombre + 10.0f;
    encabezado.avatar = m.autor;
    encabezado.idMensaje = m.id;
    encabezado.esMio = mio;
    conv.lineas.push_back(encabezado);

    for (const string& parte : ajustar(ventana, m.texto, TAM_LETRA, ANCHO_TEXTO)) {
        Linea l;
        l.principal = parte;
        l.colorPrincipal = TEXTO_OSCURO;
        l.x = SANGRIA;
        conv.lineas.push_back(l);
    }
}

// Vuelve a calcular todas las lineas a partir de los mensajes (al eliminar uno).
void Chat::reconstruir(Conversacion& conv) {
    conv.lineas.clear();

    for (const MensajeVista& m : conv.mensajes) {
        agregarLineas(conv, m);
    }
}

void Chat::agregarMensaje(Conversacion& conv, const MensajeVista& m) {
    size_t antes = conv.lineas.size();

    conv.mensajes.push_back(m);
    agregarLineas(conv, m);

    // Si estoy leyendo mensajes viejos, que la vista no salte con los nuevos.
    if (conv.desplazamiento > 0) {
        conv.desplazamiento += static_cast<int>(conv.lineas.size() - antes);
    }

    if (conv.mensajes.size() > MAX_MENSAJES) {
        conv.mensajes.erase(conv.mensajes.begin());
        reconstruir(conv);
    }
}

// La conversacion a la que pertenece un mensaje (ambito + clave, como las manda el servidor).
Conversacion* Chat::buscar(const string& ambito, const string& clave) {
    if (ambito == "G") {
        return &general;
    }

    if (ambito == "D") {
        return &directo(clave);
    }

    map<string, Conversacion>& tabla = (ambito == "R") ? grupos : canales;

    if (ambito != "R" && ambito != "C") {
        return nullptr;
    }

    auto it = tabla.find(clave);
    return it == tabla.end() ? nullptr : &it->second;
}

void Chat::procesar(const string& linea) {
    vector<string> c = separarMaximo(linea, 7);
    const string& cmd = c[0];

    if (cmd == "M" && c.size() >= 7) {
        Conversacion* conv = buscar(c[1], c[2]);

        long long id = 0;
        try { id = stoll(c[3]); } catch (...) { return; }

        if (conv == nullptr || id <= conv->ultimoId) {
            return; // conversacion desconocida o mensaje repetido
        }

        conv->ultimoId = id;
        conv->nuevos = true;
        verUsuario(c[5]);

        MensajeVista m;
        m.id = id;
        m.hora = c[4];
        m.autor = c[5];
        m.texto = c[6];
        agregarMensaje(*conv, m);

    } else if (cmd == "DEL" && c.size() >= 4) {
        Conversacion* conv = buscar(c[1], c[2]);

        long long id = 0;
        try { id = stoll(c[3]); } catch (...) { return; }

        if (conv == nullptr) return;

        for (auto it = conv->mensajes.begin(); it != conv->mensajes.end(); ++it) {
            if (it->id == id) {
                conv->mensajes.erase(it);
                reconstruir(*conv);
                break;
            }
        }

    } else if (cmd == "USERS") {
        enLinea.clear();
        string lista = (linea.size() > 6) ? linea.substr(6) : "";
        size_t inicio = 0;

        while (!lista.empty() && inicio <= lista.size()) {
            size_t coma = lista.find(',', inicio);
            if (coma == string::npos) coma = lista.size();

            string nombre = lista.substr(inicio, coma - inicio);
            if (!nombre.empty()) {
                enLinea.push_back(nombre);
                verUsuario(nombre);
            }

            inicio = coma + 1;
        }

    } else if (cmd == "GRUPO") {
        vector<string> g = separarMaximo(linea, 5);
        if (g.size() < 5) return;

        bool nuevo = !grupos.count(g[1]);
        Conversacion& conv = grupos[g[1]];
        conv.id = g[1];
        conv.titulo = g[2];
        conv.dueno = g[3];
        conv.miembros.clear();

        size_t inicio = 0;
        while (inicio <= g[4].size()) {
            size_t coma = g[4].find(',', inicio);
            if (coma == string::npos) coma = g[4].size();

            string nombre = g[4].substr(inicio, coma - inicio);
            if (!nombre.empty()) {
                conv.miembros.push_back(nombre);
                verUsuario(nombre);
            }

            inicio = coma + 1;
        }

        if (nuevo) {
            ordenGrupos.push_back(g[1]);
            conv.nuevos = true;
        }

    } else if (cmd == "CANAL") {
        vector<string> k = separarMaximo(linea, 5);
        if (k.size() < 5) return;

        bool nuevo = !canales.count(k[1]);
        Conversacion& conv = canales[k[1]];
        conv.id = k[1];
        conv.titulo = k[2];
        conv.dueno = k[3];
        conv.unido = (k[4] == "1");
        verUsuario(k[3]);

        if (nuevo) {
            ordenCanales.push_back(k[1]);
        }

    } else if (cmd == "FOTO") {
        vector<string> f = separarMaximo(linea, 3);
        if (f.size() >= 3) avatares.recibir(f[1], f[2]);

    } else if (cmd == "AVISO") {
        aviso = (linea.size() > 6) ? linea.substr(6) : "";
        relojAviso.restart();
    }
}
