// =====================================================================
//  CHAT - Servidor (solo consola, sin interfaz grafica)
//
//  Uso:   ./servidor [puerto] [archivo_base_de_datos]
//         ./servidor 5123 chat.db
//  Parar: Ctrl+C
// =====================================================================

#include "../comun/Protocolo.h"
#include "Servidor.h"

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>

using namespace std;

extern "C" void manejarSenal(int) {
    solicitarApagado(); // solo levanta una bandera (seguro dentro de un manejador de senales)
}

int main(int argc, char* argv[]) {
    int puerto = PUERTO_POR_DEFECTO;
    string archivoBD = "chat.db";

    if (argc > 1) {
        puerto = atoi(argv[1]);

        if (puerto <= 0 || puerto > 65535) {
            cout << "Uso: " << argv[0] << " [puerto] [archivo_base_de_datos]" << endl;
            return 1;
        }
    }

    if (argc > 2) {
        archivoBD = argv[2];
    }

    struct sigaction accion;
    memset(&accion, 0, sizeof(accion));
    accion.sa_handler = manejarSenal;
    sigemptyset(&accion.sa_mask);
    sigaction(SIGINT, &accion, nullptr);
    sigaction(SIGTERM, &accion, nullptr);

    Servidor servidor(puerto, archivoBD);
    return servidor.iniciar() ? 0 : 1;
}
