#ifndef CHAT_H
#define CHAT_H

#include <SFML/Graphics.hpp>
#include <map>
#include <string>
#include <vector>

#include "Avatares.h"
#include "Ventana.h"

// Medidas de la zona donde se ven los mensajes (las usa tanto Chat como mainCliente).
const float AREA_X = 425.0f;
const float AREA_Y = 70.0f;
const float AREA_ANCHO = 560.0f;
const float AREA_ALTO = 450.0f;
const float ALTO_LINEA = 25.0f;
const int LINEAS_VISIBLES = static_cast<int>(AREA_ALTO / ALTO_LINEA); // 18
const unsigned int TAM_LETRA = 16;

// Una linea YA ajustada al ancho de la zona. Un mensaje ocupa varias:
//   [avatar] Nombre  12:30        <- linea de encabezado
//            texto del mensaje... <- una o mas lineas de texto
struct Linea {
    std::string principal;   // el nombre (encabezado) o un pedazo del texto
    sf::Color colorPrincipal;
    std::string secundario;  // la hora (solo en el encabezado)
    sf::Color colorSecundario;
    float x = 0;             // donde empieza `principal` (relativo al borde de la zona)
    float xSecundario = 0;
    std::string avatar;      // usuario cuya foto se dibuja (solo en el encabezado)
};

// Una conversacion: el chat general, un directo, un grupo o un canal.
struct Conversacion {
    std::string id;                    // usuario / id de grupo / id de canal
    std::string titulo;
    std::string dueno;                 // grupos y canales
    std::vector<std::string> miembros; // grupos
    bool unido = true;                 // canales: ya me uni?
    bool nuevos = false;               // hay mensajes sin leer
    long long ultimoId = 0;            // para ignorar mensajes repetidos
    int desplazamiento = 0;            // cuantas lineas estoy "arriba" del final (0 = viendo lo ultimo)
    std::vector<Linea> lineas;
};

// Todo lo que el cliente sabe despues de iniciar sesion.
class Chat {
private:
    Ventana& ventana;
    std::vector<std::string> porPedir;

    void verUsuario(const std::string& usuario);
    void agregarMensaje(Conversacion& conv, const std::string& hora, const std::string& autor,
                        const std::string& texto);

public:
    explicit Chat(Ventana& v) : ventana(v) {}

    std::string yo;
    Conversacion general;
    std::map<std::string, Conversacion> directos; // clave: el OTRO usuario
    std::map<std::string, Conversacion> grupos;   // clave: id
    std::map<std::string, Conversacion> canales;  // clave: id
    std::vector<std::string> ordenGrupos, ordenCanales;
    std::vector<std::string> enLinea;
    Avatares avatares;

    std::string aviso;      // ultimo AVISO del servidor (se muestra unos segundos)
    sf::Clock relojAviso;

    // Interpreta UNA linea que mando el servidor (menos OK/ERR, que maneja el login).
    void procesar(const std::string& linea);

    // Usuarios cuya foto aun no se ha pedido (mainCliente manda FOTO_PEDIR).
    std::vector<std::string> fotosPorPedir();

    // Regresa la conversacion directa con `usuario` (la crea si no existe).
    Conversacion& directo(const std::string& usuario);
};

#endif

