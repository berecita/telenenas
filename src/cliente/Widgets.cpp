#include "Widgets.h"
#include <algorithm>

// Paleta rosa, basada en #FF99DC
static const sf::Color ROSA_NORMAL(255, 153, 220);   // #FF99DC
static const sf::Color ROSA_HOVER(255, 120, 207);
static const sf::Color ROSA_ACTIVO(232, 98, 186);
static const sf::Color GRIS_BORDE(238, 190, 222);
static const sf::Color TEXTO_OSCURO(74, 30, 64);     // ciruela
static const sf::Color TEXTO_SUAVE(150, 100, 138);

// ----------------------------- Boton -----------------------------

Boton::Boton() : hover(false), activo(false) {
}

void Boton::setColor(const sf::Color& normal) {
    colorNormal = normal;
    // cuando pasas el mouse el boton se oscurece tantito
    colorHover = sf::Color(static_cast<sf::Uint8>(std::max(0, normal.r - 28)),
                           static_cast<sf::Uint8>(std::max(0, normal.g - 28)),
                           static_cast<sf::Uint8>(std::max(0, normal.b - 28)));
}

Boton::Boton(const std::string& textoBoton, float x, float y, float ancho, float alto)
    : texto(textoBoton), hover(false), activo(false) {
    forma.setSize(sf::Vector2f(ancho, alto));
    forma.setPosition(x, y);
    forma.setFillColor(colorNormal);
}

void Boton::setTexto(const std::string& t) {
    texto = t;
}

void Boton::setActivo(bool a) {
    activo = a;
}

bool Boton::contiene(float x, float y) const {
    return forma.getGlobalBounds().contains(x, y);
}

void Boton::actualizarHover(float x, float y) {
    hover = contiene(x, y);
}

void Boton::dibujar(Ventana& ventana) {
    if (activo) {
        forma.setFillColor(ROSA_ACTIVO);
    } else if (hover) {
        forma.setFillColor(colorHover);
    } else {
        forma.setFillColor(colorNormal);
    }

    ventana.getWindow().draw(forma);
    ventana.dibujarTextoCentrado(texto, forma.getGlobalBounds(), 18, TEXTO_OSCURO);
}

// --------------------------- CampoTexto ---------------------------

CampoTexto::CampoTexto() : enfocado(false), oculto(false), maxLongitud(32) {
}

CampoTexto::CampoTexto(const std::string& textoEtiqueta, float x, float y,
                       float ancho, float alto, bool esOculto)
    : etiqueta(textoEtiqueta), enfocado(false), oculto(esOculto), maxLongitud(32) {
    forma.setSize(sf::Vector2f(ancho, alto));
    forma.setPosition(x, y);
    forma.setFillColor(sf::Color::White);
    forma.setOutlineThickness(2.0f);
    forma.setOutlineColor(GRIS_BORDE);
}

void CampoTexto::setEnfocado(bool e) {
    enfocado = e;
}

bool CampoTexto::estaEnfocado() const {
    return enfocado;
}

bool CampoTexto::contiene(float x, float y) const {
    return forma.getGlobalBounds().contains(x, y);
}

void CampoTexto::recibirCaracter(sf::Uint32 unicode) {
    if (!enfocado) {
        return;
    }

    if (unicode == 8) {                    // backspace: borra UN caracter (aunque tenga acento)
        while (!contenido.empty()) {
            unsigned char ultimo = static_cast<unsigned char>(contenido.back());
            contenido.pop_back();
            if ((ultimo & 0xC0) != 0x80) {
                break;
            }
        }
        if (modo == ModoCampo::FECHA_MMAA && !contenido.empty() && contenido.back() == '/') {
            contenido.pop_back(); // al borrar despues de "12/" tambien se va la diagonal
        }
        return;
    }

    if (modo != ModoCampo::LIBRE) {
        bool esDigito = (unicode >= '0' && unicode <= '9');

        if (modo == ModoCampo::SOLO_DIGITOS) {
            if (esDigito && contenido.size() < maxLongitud) {
                contenido += static_cast<char>(unicode);
            }
            return;
        }

        // FECHA_MMAA
        if (unicode == '/') {
            if (contenido.size() == 1 && contenido[0] >= '1') { // "5/" -> "05/"
                contenido = "0" + contenido + "/";
            }
            return;
        }

        if (!esDigito || contenido.size() >= 5) {
            return;
        }

        if (contenido.empty() && unicode >= '2') { // "5" -> "05/" (no existe el mes 5x)
            contenido = std::string("0") + static_cast<char>(unicode) + "/";
            return;
        }

        contenido += static_cast<char>(unicode);

        if (contenido.size() == 2) {
            contenido += '/';
        }
        return;
    }

    std::string bytes;

    if (unicode >= 32 && unicode < 127) {           // ASCII que se puede imprimir
        bytes += static_cast<char>(unicode);
    } else if (unicode >= 0xA0 && unicode <= 0x24F) { // letras con acento, ñ, ¿, ¡, etc. (UTF-8 de 2 bytes)
        bytes += static_cast<char>(0xC0 | (unicode >> 6));
        bytes += static_cast<char>(0x80 | (unicode & 0x3F));
    } else {
        return; // otros caracteres (emojis, control) se ignoran
    }

    if (contenido.size() + bytes.size() <= maxLongitud) {
        contenido += bytes;
    }
    // El Enter (13) y el Tab (9) los ve quien use el campo, no el campo en si.
}

const std::string& CampoTexto::getContenido() const {
    return contenido;
}

void CampoTexto::setContenido(const std::string& texto) {
    contenido = texto;
}

void CampoTexto::setMaxLongitud(std::size_t maximo) {
    maxLongitud = maximo;
}

void CampoTexto::setModo(ModoCampo m) {
    modo = m;
}

void CampoTexto::limpiar() {
    contenido.clear();
}

void CampoTexto::dibujar(Ventana& ventana) {
    forma.setOutlineColor(enfocado ? ROSA_NORMAL : GRIS_BORDE);
    ventana.getWindow().draw(forma);

    sf::FloatRect r = forma.getGlobalBounds();

    // Etiqueta arriba del campo
    ventana.dibujarTexto(etiqueta, r.left, r.top - 24.0f, 16, TEXTO_SUAVE);

    // Contenido (asteriscos si es contrasena) + cursor "_" si tiene el foco
    std::string mostrado = oculto ? std::string(contenido.size(), '*') : contenido;
    if (enfocado) {
        mostrado += "_";
    }

    // Si el texto es mas largo que la caja, solo se muestra el FINAL (lo que se esta
    // escribiendo), quitando caracteres UTF-8 completos del principio.
    const float disponible = r.width - 20.0f;
    while (mostrado.size() > 1 && ventana.medirTexto(mostrado, 20) > disponible) {
        size_t i = 1;
        while (i < mostrado.size() && (static_cast<unsigned char>(mostrado[i]) & 0xC0) == 0x80) {
            i++;
        }
        mostrado.erase(0, i);
    }

    ventana.dibujarTexto(mostrado, r.left + 10.0f, r.top + 8.0f, 20, TEXTO_OSCURO);
}
