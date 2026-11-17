#ifndef CONFIG_H
#define CONFIG_H

#include <cstdlib>
#include <string>

// ---------------------------------------------------------------------
// Valores por defecto del cliente. Como servidor y cliente corren en la
// MISMA maquina (la del profe), la IP por defecto es 127.0.0.1.
// Se pueden cambiar sin recompilar:
//   ./cliente                    -> 127.0.0.1:5000
//   ./cliente 10.0.0.5           -> 10.0.0.5:5000
//   ./cliente 10.0.0.5 6000      -> 10.0.0.5:6000
// (tambien se pueden editar dentro de la ventana de login)
// ---------------------------------------------------------------------
#define IP_SERVIDOR_POR_DEFECTO "127.0.0.1"
#define PUERTO_POR_DEFECTO 5000

inline std::string resolverIp(int argc, char* argv[]) {
	if (argc > 1 && argv[1] != nullptr && argv[1][0] != '\0') {
		return argv[1];
	}
	return IP_SERVIDOR_POR_DEFECTO;
}

inline int resolverPuerto(int argc, char* argv[]) {
	if (argc > 2 && argv[2] != nullptr) {
		int p = std::atoi(argv[2]);
		if (p > 0 && p < 65536) {
			return p;
		}
	}
	return PUERTO_POR_DEFECTO;
}

#endif

