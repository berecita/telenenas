#include "Base64.h"

static const char ALFABETO[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Codificar(const unsigned char* datos, std::size_t tamano) {
    std::string salida;
    salida.reserve(((tamano + 2) / 3) * 4);

    std::size_t i = 0;

    while (i + 2 < tamano) {
        unsigned int v = (datos[i] << 16) | (datos[i + 1] << 8) | datos[i + 2];
        salida += ALFABETO[(v >> 18) & 63];
        salida += ALFABETO[(v >> 12) & 63];
        salida += ALFABETO[(v >> 6) & 63];
        salida += ALFABETO[v & 63];
        i += 3;
    }

    if (i + 1 == tamano) {
        unsigned int v = datos[i] << 16;
        salida += ALFABETO[(v >> 18) & 63];
        salida += ALFABETO[(v >> 12) & 63];
        salida += "==";
    } else if (i + 2 == tamano) {
        unsigned int v = (datos[i] << 16) | (datos[i + 1] << 8);
        salida += ALFABETO[(v >> 18) & 63];
        salida += ALFABETO[(v >> 12) & 63];
        salida += ALFABETO[(v >> 6) & 63];
        salida += '=';
    }

    return salida;
}

bool base64Decodificar(const std::string& texto, std::string& salida) {
    salida.clear();

    if (texto.size() % 4 != 0) {
        return false;
    }

    int tabla[256];
    for (int i = 0; i < 256; i++) tabla[i] = -1;
    for (int i = 0; i < 64; i++) tabla[static_cast<unsigned char>(ALFABETO[i])] = i;

    for (std::size_t i = 0; i < texto.size(); i += 4) {
        int v[4];
        int relleno = 0;

        for (int j = 0; j < 4; j++) {
            unsigned char c = static_cast<unsigned char>(texto[i + j]);

            if (c == '=') {
                // el relleno solo es valido en las ultimas 2 posiciones del ultimo bloque
                if (i + 4 != texto.size() || j < 2) return false;
                v[j] = 0;
                relleno++;
            } else {
                if (relleno > 0 || tabla[c] < 0) return false;
                v[j] = tabla[c];
            }
        }

        unsigned int n = (v[0] << 18) | (v[1] << 12) | (v[2] << 6) | v[3];
        salida += static_cast<char>((n >> 16) & 0xFF);
        if (relleno < 2) salida += static_cast<char>((n >> 8) & 0xFF);
        if (relleno < 1) salida += static_cast<char>(n & 0xFF);
    }

    return true;
}

