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
// Si alguien no tiene foto, dibuja un circulo de color con su inicial.
class Avatares {
private:
    std::map<std::string, sf::Texture> texturas; // clave: usuario en minusculas
    std::set<std::string> sinFoto;
    std::set<std::string> pedidas;

public:
    // true UNA sola vez por usuario: indica que hay que mandar FOTO_PEDIR.
    bool necesitaPedir(const std::string& usuario);

    // Llega "FOTO|usuario|base64" (base64 vacio = sin foto).
    void recibir(const std::string& usuario, const std::string& base64);

    void dibujar(Ventana& ventana, const std::string& usuario, float x, float y, float diametro);
};

#endif

