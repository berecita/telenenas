#ifndef CONEXION_H
#define CONEXION_H

#include <SFML/Network.hpp>
#include <string>

// ---------------------------------------------------------------------
// Conexin TCP del cliente usando sf::TcpSocket (SFML Network).
//
// Por que sf::TcpSocket y no sockets de Linux: el cliente tiene que correr en
// Windows, Linux y macOS, y SFML ya tapa las diferencias (Winsock, etc.).
//
// Funciona SIN HILOS: el socket no bloquea y el ciclo de la ventana
// llama a actualizar() en cada cuadro, que manda lo que falta y lee lo que
// llego. Asi la ventana nunca se congela esperando al servidor.
// ---------------------------------------------------------------------
class Conexion {
private:
    sf::TcpSocket socket;
    bool activa = false;
    std::string entrada; // bytes que llegaron pero todavia no forman una linea completa
    std::string salida;  // bytes que faltan por mandar

public:
    // Se conecta esperando maximo `milisegundos` (OJO: esto SI bloquea ese rato).
    bool conectar(const std::string& direccion, unsigned short puerto, int milisegundos,
                  std::string& error);

    void enviar(const std::string& linea); // agrega '\n' y lo manda en cuanto se pueda
    void actualizar();                     // llamar una vez por cuadro
    bool siguienteLinea(std::string& linea);

    bool estaActiva() const { return activa; }
    void cerrar();
};

#endif
