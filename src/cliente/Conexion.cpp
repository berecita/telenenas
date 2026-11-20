#include "Conexion.h"

#include "../comun/Protocolo.h"

bool Conexion::conectar(const std::string& direccion, unsigned short puerto, int milisegundos,
                        std::string& error) {
    cerrar();

    sf::IpAddress ip(direccion); // acepta "192.168.1.5" y tambien nombres de host (ahi hace DNS)

    if (ip == sf::IpAddress::None) {
        error = "No se pudo resolver la direccion '" + direccion + "'.";
        return false;
    }

    socket.setBlocking(true);

    if (socket.connect(ip, puerto, sf::milliseconds(milisegundos)) != sf::Socket::Done) {
        error = "No se pudo conectar a " + direccion + ":" + std::to_string(puerto) +
                " (revisa la IP, el puerto y que el servidor este encendido).";
        return false;
    }

    socket.setBlocking(false); // desde aqui nunca se espera nada
    activa = true;
    entrada.clear();
    salida.clear();
    return true;
}

void Conexion::enviar(const std::string& linea) {
    if (!activa) return;
    salida += linea;
    salida += '\n';
}

void Conexion::actualizar() {
    if (!activa) return;

    // ---- mandar lo que falta ----
    while (!salida.empty()) {
        std::size_t enviados = 0;
        sf::Socket::Status estado = socket.send(salida.data(), salida.size(), enviados);

        if (enviados > 0) {
            salida.erase(0, enviados);
        }

        if (estado == sf::Socket::Disconnected || estado == sf::Socket::Error) {
            activa = false;
            return;
        }

        if (estado != sf::Socket::Done) {
            break; // NotReady / Partial: se vuelve a intentar en el siguiente cuadro
        }
    }

    // ---- recibir lo que haya llegado ----
    char buffer[8192];

    for (int i = 0; i < 64; i++) { // tope por cuadro pa no acaparar todo el ciclo
        std::size_t recibidos = 0;
        sf::Socket::Status estado = socket.receive(buffer, sizeof(buffer), recibidos);

        if (estado == sf::Socket::Done) {
            entrada.append(buffer, recibidos);

            if (entrada.size() > 4 * MAX_LINEA && entrada.find('\n') == std::string::npos) {
                activa = false; // el servidor mando pura basura sin saltos de linea
                return;
            }
        } else if (estado == sf::Socket::NotReady) {
            break;
        } else {
            activa = false; // Disconnected o Error
            break;
        }
    }
}

bool Conexion::siguienteLinea(std::string& linea) {
    std::size_t pos = entrada.find('\n');

    if (pos == std::string::npos) {
        return false;
    }

    linea = entrada.substr(0, pos);
    entrada.erase(0, pos + 1);

    if (!linea.empty() && linea.back() == '\r') {
        linea.pop_back();
    }

    return true;
}

void Conexion::cerrar() {
    if (activa) {
        socket.disconnect();
    }

    activa = false;
    entrada.clear();
    salida.clear();
}
