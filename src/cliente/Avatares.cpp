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
    // Windows "Copiar como ruta" la entrega entre comillas.
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

    // Recorte cuadrado centrado.
    const unsigned int x0 = (ancho - lado) / 2;
    const unsigned int y0 = (alto - lado) / 2;

    // Reducir a 64x64 promediando los pixeles de cada bloque (se ve mejor que saltarse pixeles).
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

bool Avatares::necesitaPedir(const std::string& usuario) {
    return pedidas.insert(minusculas(usuario)).second;
}

void Avatares::recibir(const std::string& usuario, const std::string& base64) {
    std::string clave = minusculas(usuario);
    pedidas.insert(clave);

    std::string bytes;

    if (base64.empty() || !base64Decodificar(base64, bytes) || bytes.size() != FOTO_BYTES) {
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

void Avatares::dibujar(Ventana& ventana, const std::string& usuario, float x, float y, float diametro) {
    std::string clave = minusculas(usuario);

    sf::CircleShape circulo(diametro / 2.0f);
    circulo.setPosition(x, y);

    auto it = texturas.find(clave);

    if (it != texturas.end()) {
        circulo.setTexture(&it->second, true); // la foto se recorta en forma de circulo
        ventana.getWindow().draw(circulo);
        return;
    }

    // Sin foto (o aun no llega): circulo de color segun el nombre + inicial.
    static const sf::Color PALETA[] = {
        sf::Color(111, 78, 55), sf::Color(60, 110, 180), sf::Color(40, 160, 90), sf::Color(180, 90, 60),
        sf::Color(130, 60, 160), sf::Color(190, 140, 40), sf::Color(50, 140, 150), sf::Color(170, 70, 110)};

    unsigned int suma = 0;
    for (unsigned char c : clave) suma = suma * 31 + c;

    circulo.setFillColor(PALETA[suma % 8]);
    ventana.getWindow().draw(circulo);

    // Inicial (primer caracter UTF-8 completo).
    std::string inicial;

    if (!usuario.empty()) {
        std::size_t n = 1;
        while (n < usuario.size() && (static_cast<unsigned char>(usuario[n]) & 0xC0) == 0x80) n++;
        inicial = usuario.substr(0, n);

        if (n == 1) inicial[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(inicial[0])));
    }

    ventana.dibujarTextoCentrado(inicial, sf::FloatRect(x, y - 1.0f, diametro, diametro),
                                 static_cast<unsigned int>(diametro * 0.55f), sf::Color::White);
}

