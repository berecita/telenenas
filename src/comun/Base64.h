#ifndef BASE64_H
#define BASE64_H

#include <string>

// Base64 normalito (A-Z a-z 0-9 + /), con relleno '='.
std::string base64Codificar(const unsigned char* datos, std::size_t tamano);

// Regresa false si el texto no es base64 valdio, osea que esta mal.
bool base64Decodificar(const std::string& texto, std::string& salida);

#endif
