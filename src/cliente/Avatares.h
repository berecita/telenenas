#ifndef AVATARES_H
#define AVATARES_H

#include <SFML/Graphics.hpp>
#include <map>
#include <set>
#include <string>

#include "Ventana.h"

// Agarra una imagen del disco (png, jpg, bmp, tga...) y la convierte en la foto de perfil que
// se manda al servidor: recorte cuadrado al centro, reducida a 64x64, en RGBA
// crudo y en base64. Regresa false (y el motivo) si no se pudo.
bool prepararFoto(const std::string& ruta, std::string& base64, std::string& error);

// Guarda las fotos de perfil que ya llegaron (como texturas) y sabe dibujarlas.
// El "perfil" que manda el servidor puede ser:
//     ""            -> sin foto: dibuja un circulito de color con la inicial
//     "animal:gato" -> uno de los avatares de default (recursos/avatares/gato.png)
//     base64        -> tu foto de 64x64 RGBA
class Avatares {
private:
    std::map<std::string, sf::Texture> texturas;       // usuario (en minusculas) -> su foto
    std::map<std::string, sf::Texture> predeterminados; // gato -> su textura
    std::set<std::string> sinFoto;
    std::set<std::string> pedidas;

    void dibujarCirculo(Ventana& ventana, const sf::Texture* textura, float x, float y, float diametro,
                        const std::string& nombre);

public:
    // Carga los 5 animalitos de recursos/avatares/. Da false si falta alguno.
    bool cargarPredeterminados();

    // Se olvida de las fotos de los usuarios (se usa cuando se inicia una sesion nueva).
    void limpiarUsuarios();

    // da true UNA sola vez por usuario: osea que hay que mandar FOTO_PEDIR.
    bool necesitaPedir(const std::string& usuario);

    // Cuando llega "FOTO|usuario|perfil".
    void recibir(const std::string& usuario, const std::string& perfil);

    void dibujar(Ventana& ventana, const std::string& usuario, float x, float y, float diametro);
    void dibujarPredeterminado(Ventana& ventana, const std::string& animal, float x, float y, float diametro);
};

#endif
