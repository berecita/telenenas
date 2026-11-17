#include "Chat.h"

#include "../comun/Protocolo.h"

using namespace std;

static const sf::Color TEXTO_OSCURO(50, 40, 35);
static const sf::Color TEXTO_SUAVE(130, 120, 110);
static const sf::Color VERDE(40, 160, 90);
static const sf::Color AZUL(60, 110, 180);

static const float SANGRIA = 32.0f;                              // espacio del avatar
static const float ANCHO_TEXTO = AREA_ANCHO - 10.0f - SANGRIA - 18.0f; // margen derecho y barra de scroll
static const size_t MAX_LINEAS = 1500;                           // historial maximo por conversacion

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

void Chat::agregarMensaje(Conversacion& conv, const string& hora, const string& autor, const string& texto) {
    bool mio = (autor == yo);
    float anchoNombre = ventana.medirTexto(autor, TAM_LETRA);

    Linea encabezado;
    encabezado.principal = autor;
    encabezado.colorPrincipal = mio ? VERDE : AZUL;
    encabezado.secundario = hora;
    encabezado.colorSecundario = TEXTO_SUAVE;
    encabezado.x = SANGRIA;
    encabezado.xSecundario = SANGRIA + anchoNombre + 10.0f;
    encabezado.avatar = autor;
    conv.lineas.push_back(encabezado);

    vector<string> partes = ajustar(ventana, texto, TAM_LETRA, ANCHO_TEXTO);

    for (const string& parte : partes) {
        Linea l;
        l.principal = parte;
        l.colorPrincipal = TEXTO_OSCURO;
        l.x = SANGRIA;
        conv.lineas.push_back(l);
    }

    // Si estoy leyendo mensajes viejos, que la vista no salte con los nuevos.
    if (conv.desplazamiento > 0) {
        conv.desplazamiento += static_cast<int>(partes.size()) + 1;
    }

    if (conv.lineas.size() > MAX_LINEAS) {
        conv.lineas.erase(conv.lineas.begin(), conv.lineas.begin() + (conv.lineas.size() - MAX_LINEAS));
    }
}

void Chat::procesar(const string& linea) {
    vector<string> c = separarMaximo(linea, 7);
    const string& cmd = c[0];

    if (cmd == "M" && c.size() >= 7) {
        const string& ambito = c[1];
        const string& clave = c[2];
        Conversacion* conv = nullptr;

        if (ambito == "G") {
            conv = &general;
        } else if (ambito == "D") {
            conv = &directo(clave);
        } else if (ambito == "R") {
            auto it = grupos.find(clave);
            if (it != grupos.end()) conv = &it->second;
        } else if (ambito == "C") {
            auto it = canales.find(clave);
            if (it != canales.end()) conv = &it->second;
        }

        long long id = 0;
        try { id = stoll(c[3]); } catch (...) { return; }

        if (conv == nullptr || id <= conv->ultimoId) {
            return; // conversacion desconocida o mensaje repetido
        }

        conv->ultimoId = id;
        conv->nuevos = true;
        verUsuario(c[5]);
        agregarMensaje(*conv, c[4], c[5], c[6]);

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

