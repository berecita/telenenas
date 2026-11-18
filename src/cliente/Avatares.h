#ifndef AVATARES_H
#define AVATARES_H

#include <SFML/Graphics.hpp>
#include <map>
#include <set>
#include <string>

#include "Ventana.h"

// Convierte una imagen del disco (png, jpg, bmp, tga...) en la foto de perfil que
// se manda al servidor: recorte cuadrado centrado, reducida a 64x64, en RGBA
// crudo y codificada en base64. Regresa false (y el motivo) si no se pudo.
bool prepararFoto(const std::string& ruta, std::string& base64, std::string& error);

// Guarda las fotos de perfil ya recibidas (como texturas) y sabe dibujarlas.
// Un "perfil" que manda el servidor puede ser:
//     ""            -> sin foto: se dibuja un circulo de color con la inicial
//     "animal:gato" -> uno de los avatares predeterminados (recursos/avatares/gato.png)
//     base64        -> foto propia de 64x64 RGBA
class Avatares {
private:
    std::map<std::string, sf::Texture> texturas;       // usuario (minusculas) -> su foto
    std::map<std::string, sf::Texture> predeterminados; // "gato" -> textura
    std::set<std::string> sinFoto;
    std::set<std::string> pedidas;

    void dibujarCirculo(Ventana& ventana, const sf::Texture* textura, float x, float y, float diametro,
                        const std::string& nombre);

public:
    // Carga los 5 animalitos de recursos/avatares/. Regresa false si falta alguno.
    bool cargarPredeterminados();

    // Olvida las fotos de usuarios (se usa al iniciar una sesion nueva).
    void limpiarUsuarios();

    // true UNA sola vez por usuario: indica que hay que mandar FOTO_PEDIR.
    bool necesitaPedir(const std::string& usuario);

    // Llega "FOTO|usuario|perfil".
    void recibir(const std::string& usuario, const std::string& perfil);

    void dibujar(Ventana& ventana, const std::string& usuario, float x, float y, float diametro);
    void dibujarPredeterminado(Ventana& ventana, const std::string& animal, float x, float y, float diametro);
};

#endif
