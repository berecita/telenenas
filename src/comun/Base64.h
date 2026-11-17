#ifndef BASE64_H
#define BASE64_H

#include <string>

// Base64 estandar (A-Z a-z 0-9 + /), con relleno '='.
std::string base64Codificar(const unsigned char* datos, std::size_t tamano);

// Regresa false si el texto no es base64 valido.
bool base64Decodificar(const std::string& texto, std::string& salida);

#endif

