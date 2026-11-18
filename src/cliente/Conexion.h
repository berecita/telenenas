#ifndef CONEXION_H
#define CONEXION_H

#include <SFML/Network.hpp>
#include <string>

// ---------------------------------------------------------------------
// Conexion TCP del cliente con sf::TcpSocket (SFML Network).
//
// Por que sf::TcpSocket y no sockets de Linux: este cliente debe correr en
// Windows, Linux y macOS, y SFML ya esconde las diferencias (Winsock, etc.).
//
// Funciona SIN HILOS: el socket es no bloqueante y el ciclo de la ventana
// llama a actualizar() en cada cuadro, que manda lo pendiente y lee lo que
// haya llegado. Asi la ventana nunca se congela esperando al servidor.
// ---------------------------------------------------------------------
class Conexion {
private:
    sf::TcpSocket socket;
    bool activa = false;
    std::string entrada; // bytes recibidos que aun no forman una linea completa
    std::string salida;  // bytes pendientes de enviar

public:
    // Se conecta esperando como maximo `milisegundos` (esto SI bloquea ese tiempo).
    bool conectar(const std::string& direccion, unsigned short puerto, int milisegundos,
                  std::string& error);

    void enviar(const std::string& linea); // agrega '\n' y lo manda en cuanto se pueda
    void actualizar();                     // llamar una vez por cuadro
    bool siguienteLinea(std::string& linea);

    bool estaActiva() const { return activa; }
    void cerrar();
};

#endif
