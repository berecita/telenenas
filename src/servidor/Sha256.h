#ifndef SHA256_H
#define SHA256_H

#include <string>

// SHA-256 (FIPS 180-4). Regresa el resumen en hexadecimal (64 caracteres).
std::string sha256Hex(const std::string& datos);

#endif

