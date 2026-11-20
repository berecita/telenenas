#include "Avatares.h"

#include "../comun/Base64.h"
#include "../comun/Protocolo.h"

#include <algorithm>
#include <cctype>
#include <vector>

static std::string minusculas(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

bool prepararFoto(const std::string& rutaCruda, std::string& base64, std::string& error) {
    // En Windows "Copiar como ruta" la da entre comillas, hay que quitarselas.
    std::string ruta = rutaCruda;
    if (ruta.size() >= 2 && ruta.front() == '"' && ruta.back() == '"') {
        ruta = ruta.substr(1, ruta.size() - 2);
    }

    sf::Image original;

    if (!original.loadFromFile(ruta)) {
        error = "No se pudo abrir la imagen '" + ruta + "' (usa png, jpg, bmp o tga).";
        return false;
    }

    const unsigned int ancho = original.getSize().x;
    const unsigned int alto = original.getSize().y;
    const unsigned int lado = std::min(ancho, alto);

    if (lado == 0) {
        error = "La imagen esta vacia.";
        return false;
    }

    // Recortamos cuadrado y al centro.
    const unsigned int x0 = (ancho - lado) / 2;
    const unsigned int y0 = (alto - lado) / 2;

    // Se reduce a 64x64 sacando el promedaindo de los pixeles de cada bloque (se ve mas bonito que nomas saltarse pixeles).
    std::vector<sf::Uint8> pixeles(FOTO_BYTES);

    for (int py = 0; py < FOTO_LADO; py++) {
        for (int px = 0; px < FOTO_LADO; px++) {
            unsigned int ini_x = x0 + static_cast<unsigned int>(static_cast<unsigned long long>(px) * lado / FOTO_LADO);
            unsigned int fin_x = x0 + static_cast<unsigned int>(static_cast<unsigned long long>(px + 1) * lado / FOTO_LADO);
            unsigned int ini_y = y0 + static_cast<unsigned int>(static_cast<unsigned long long>(py) * lado / FOTO_LADO);
            unsigned int fin_y = y0 + static_cast<unsigned int>(static_cast<unsigned long long>(py + 1) * lado / FOTO_LADO);

            if (fin_x <= ini_x) fin_x = ini_x + 1;
            if (fin_y <= ini_y) fin_y = ini_y + 1;

            unsigned long long r = 0, g = 0, b = 0, a = 0, n = 0;

            for (unsigned int y = ini_y; y < fin_y && y < alto; y++) {
                for (unsigned int x = ini_x; x < fin_x && x < ancho; x++) {
                    sf::Color c = original.getPixel(x, y);
                    r += c.r; g += c.g; b += c.b; a += c.a;
                    n++;
                }
            }

            if (n == 0) n = 1;

            std::size_t i = (static_cast<std::size_t>(py) * FOTO_LADO + px) * 4;
            pixeles[i] = static_cast<sf::Uint8>(r / n);
            pixeles[i + 1] = static_cast<sf::Uint8>(g / n);
            pixeles[i + 2] = static_cast<sf::Uint8>(b / n);
            pixeles[i + 3] = static_cast<sf::Uint8>(a / n);
        }
    }

    base64 = base64Codificar(pixeles.data(), pixeles.size());
    return true;
}

static bool cargarRecurso(sf::Texture& textura, const std::string& relativa) {
    std::vector<std::string> candidatas;

#ifdef RECURSOS_DIR
    candidatas.push_back(std::string(RECURSOS_DIR) + "/" + relativa); // ruta absoluta que viene desde CMake
#endif
    candidatas.push_back("recursos/" + relativa);
    candidatas.push_back("../recursos/" + relativa);
    candidatas.push_back("../../recursos/" + relativa);

    for (const std::string& ruta : candidatas) {
        if (textura.loadFromFile(ruta)) {
            return true;
        }
    }

    return false;
}

bool Avatares::cargarPredeterminados() {
    bool todos = true;

    for (const char* animal : AVATARES_PREDETERMINADOS) {
        sf::Texture& textura = predeterminados[animal];

        if (cargarRecurso(textura, std::string("avatares/") + animal + ".png")) {
            textura.setSmooth(true);
        } else {
            predeterminados.erase(animal);
            todos = false;
        }
    }

    return todos;
}

void Avatares::limpiarUsuarios() {
    texturas.clear();
    sinFoto.clear();
    pedidas.clear();
}

bool Avatares::necesitaPedir(const std::string& usuario) {
    return pedidas.insert(minusculas(usuario)).second;
}

void Avatares::recibir(const std::string& usuario, const std::string& perfil) {
    std::string clave = minusculas(usuario);
    pedidas.insert(clave);

    // Avatar de default: reusamos la imagen que ya estaba cargada.
    if (perfil.rfind("animal:", 0) == 0) {
        auto it = predeterminados.find(perfil.substr(7));

        if (it != predeterminados.end()) {
            texturas[clave] = it->second;
            sinFoto.erase(clave);
        } else {
            sinFoto.insert(clave);
        }

        return;
    }

    // Foto propia en base64.
    std::string bytes;

    if (perfil.empty() || !base64Decodificar(perfil, bytes) || bytes.size() != FOTO_BYTES) {
        sinFoto.insert(clave);
        return;
    }

    sf::Image imagen;
    imagen.create(FOTO_LADO, FOTO_LADO, reinterpret_cast<const sf::Uint8*>(bytes.data()));

    sf::Texture& textura = texturas[clave];

    if (textura.loadFromImage(imagen)) {
        textura.setSmooth(true);
        sinFoto.erase(clave);
    } else {
        texturas.erase(clave);
        sinFoto.insert(clave);
    }
}

// Dibuja la textura recortada en circulo; si no hay, pone un circulo de color con la inicial.
void Avatares::dibujarCirculo(Ventana& ventana, const sf::Texture* textura, float x, float y,
                              float diametro, const std::string& nombre) {
    sf::CircleShape circulo(diametro / 2.0f);
    circulo.setPosition(x, y);

    if (textura != nullptr) {
        circulo.setTexture(textura, true); // la imagen cuadrada se ve recortada como circulo
        ventana.getWindow().draw(circulo);
        return;
    }

    // Colores rosas/lilas, uno por cada nombre.
    static const sf::Color PALETA[] = {
        sf::Color(214, 82, 170), sf::Color(150, 110, 210), sf::Color(235, 110, 140), sf::Color(190, 90, 200),
        sf::Color(240, 130, 180), sf::Color(120, 130, 220), sf::Color(220, 100, 120), sf::Color(170, 100, 170)};

    unsigned int suma = 0;
    for (unsigned char c : minusculas(nombre)) suma = suma * 31 + c;

    circulo.setFillColor(PALETA[suma % 8]);
    ventana.getWindow().draw(circulo);

    // La inicial (primer caracter UTF-8 completito).
    std::string inicial;

    if (!nombre.empty()) {
        std::size_t n = 1;
        while (n < nombre.size() && (static_cast<unsigned char>(nombre[n]) & 0xC0) == 0x80) n++;
        inicial = nombre.substr(0, n);

        if (n == 1) inicial[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(inicial[0])));
    }

    ventana.dibujarTextoCentrado(inicial, sf::FloatRect(x, y - 1.0f, diametro, diametro),
                                 static_cast<unsigned int>(diametro * 0.55f), sf::Color::White);
}

void Avatares::dibujar(Ventana& ventana, const std::string& usuario, float x, float y, float diametro) {
    auto it = texturas.find(minusculas(usuario));
    dibujarCirculo(ventana, it != texturas.end() ? &it->second : nullptr, x, y, diametro, usuario);
}

void Avatares::dibujarPredeterminado(Ventana& ventana, const std::string& animal, float x, float y,
                                     float diametro) {
    auto it = predeterminados.find(animal);
    dibujarCirculo(ventana, it != predeterminados.end() ? &it->second : nullptr, x, y, diametro, animal);
}
