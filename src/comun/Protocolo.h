#ifndef PROTOCOLO_H
#define PROTOCOLO_H

// =====================================================================
//  Protocolo del chat (compartido por servidor y cliente)
//
//  Todo es TEXTO: un mensaje = una linea terminada en '\n', con campos
//  separados por '|' (igual que en upifood). El ultimo campo de cada
//  mensaje puede llevar '|' sin problema.
//
//  CLIENTE -> SERVIDOR
//    REGISTRO|usuario|clave|foto      crea la cuenta y entra (foto = base64 o vacio)
//    LOGIN|usuario|clave              entra con una cuenta existente
//    GENERAL|texto                    mensaje al chat general
//    DM|destino|texto                 mensaje directo
//    GRUPO_NUEVO|nombre|m1,m2,...     crea un grupo con esos miembros
//    GMSG|idGrupo|texto               mensaje a un grupo
//    CANAL_NUEVO|nombre               crea un canal (el creador es el dueño)
//    CANAL_UNIR|idCanal               se une a un canal
//    CMSG|idCanal|texto               mensaje a un canal (solo el dueño)
//    FOTO_PEDIR|usuario               pide la foto de perfil de alguien
//    SALIR
//
//  SERVIDOR -> CLIENTE
//    OK|usuario                       login/registro correcto
//    ERR|motivo                       login/registro fallido
//    AVISO|texto                      error o aviso de cualquier comando
//    USERS|a,b,c                      usuarios en linea
//    M|ambito|clave|id|hora|autor|texto
//          ambito: G general, D directo, R grupo, C canal
//          clave : "general" / el OTRO usuario / id de grupo / id de canal
//    GRUPO|id|nombre|dueno|m1,m2,...
//    CANAL|id|nombre|dueno|unido(0/1)
//    FOTO|usuario|base64              (base64 vacio = no tiene foto)
// =====================================================================

#include <cstddef>
#include <string>
#include <vector>

const int PUERTO_POR_DEFECTO = 5000;

// La foto de perfil se reduce en el cliente a FOTO_LADO x FOTO_LADO pixeles
// RGBA crudos (sin formato PNG/JPG) y se manda en base64.
const int FOTO_LADO = 64;
const std::size_t FOTO_BYTES = static_cast<std::size_t>(FOTO_LADO) * FOTO_LADO * 4;
const std::size_t FOTO_B64_LONGITUD = ((FOTO_BYTES + 2) / 3) * 4;

const std::size_t MAX_LINEA = 65536;   // bytes por linea del protocolo
const std::size_t MAX_TEXTO = 500;     // bytes por mensaje de chat
const std::size_t MAX_NOMBRE_USUARIO = 16;
const std::size_t MAX_NOMBRE_SALA = 24; // grupos y canales
const std::size_t MIN_CLAVE = 4;
const std::size_t MAX_CLAVE = 64;

// Separa "a|b|c|d" en maximo N campos; el ultimo se queda con TODO lo que
// sobre.  separarMaximo("M|G|x|hola | adios", 4) -> {"M","G","x","hola | adios"}
inline std::vector<std::string> separarMaximo(const std::string& mensaje, std::size_t maxCampos,
                                              char separador = '|') {
    std::vector<std::string> campos;
    std::size_t inicio = 0;

    while (campos.size() + 1 < maxCampos) {
        std::size_t pos = mensaje.find(separador, inicio);

        if (pos == std::string::npos) {
            break;
        }

        campos.push_back(mensaje.substr(inicio, pos - inicio));
        inicio = pos + 1;
    }

    campos.push_back(mensaje.substr(inicio));
    return campos;
}

#endif

