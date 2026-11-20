#ifndef PROTOCOLO_H
#define PROTOCOLO_H

// =====================================================================
//  Protocolo del chat (lo usan el servidor y el cliente, los dos)
//
//  Todo es TEXTO: un mensaje = una linea que acaba en '\n', con los campos
//  separados por '|' (igual que en upifood). El ultimo campo puede traer
//  '|' y no pasa nada, tranqui.
//
//  CLIENTE -> SERVIDOR
//    REGISTRO|usuario|clave|perfil    crea la cuenta y te mete. perfil = "" (sin foto),
//                                     "animal:gato" (avatar de los de default) o base64 (tu foto)
//    LOGIN|usuario|clave              entras con una cuenta que ya existe
//    GENERAL|texto                    mensaje pal chat general
//    DM|destino|texto                 mensge directo
//    GRUPO_NUEVO|nombre|m1,m2,...     crea un grupo con esos miembros
//    GMSG|idGrupo|texto               mensaje a un grupo
//    GRUPO_AGREGAR|idGrupo|usuario     el dueno mete a alguien (aunque no este conectado)
//    GRUPO_QUITAR|idGrupo|usuario      el dueno saca a alguien
//    GRUPO_SALIR|idGrupo               un integrante se sale (el dueno no puede)
//    GRUPO_ELIMINAR|idGrupo            solo el dueno borra el grupo y todo su historial
//    CANAL_NUEVO|nombre               crea un canal (quien lo crea es el dueño)
//    CANAL_UNIR|idCanal               te unes a un canal
//    CANAL_MIEMBROS_PEDIR|idCanal      consuta privada, solo pa el dueno
//    CANAL_ELIMINAR|idCanal            solo el dueno borra el canal
//    CUENTA_ELIMINAR|contrasena        borra tu cuenta, antes revalida la clave
//    CMSG|idCanal|texto               mensaje a un canal (solo escribe el dueño)
//    BORRAR|idMensaje                 borra un mensaje TUYO (para todos)
//    MARCAR_LEIDO|ambito|clave|id      avisa cual fue el ultimo mesaje que viste
//    FOTO_PEDIR|usuario               pide la foto de perfil de alguien
//    SALIR
//
//  SERVIDOR -> CLIENTE
//    OK|usuario                       login/registro salio bien
//    ERR|motivo                       login/registro fallo
//    AVISO|texto                      error o aviso de cualquier comando
//    USERS|a,b,c                      usuarios en linea
//    LECTURA|ambito|clave|ultimoId    marcador de lectura que se queda guardado
//    M|ambito|clave|id|timestampUnix|autor|texto  (segundos desde 1970, UTC;
//                                                 el cliente lo pasa a su hora local)
//          ambito: G general, D directo, R grupo, C canal
//          clave : "general" / el OTRO usuario / id del grupo / id del canal
//    GRUPO|id|nombre|dueno|m1,m2,...
//    CANAL|id|nombre|dueno|unido(0/1)
//    CANAL_MIEMBROS_INICIO|id         aqui empieza la lista privada de suscriptores
//    CANAL_MIEMBRO|id|usuario         un suscriptor (una linea x persona)
//    CANAL_MIEMBROS_FIN|id            ya acabo la lista
//    MD|ambito|clave|id|timestampUnix|autor  constancia de que borraron un mensaje
//    GRUPO_QUITADO|id                 te sacaron del grupo o tu te saliste
//    GRUPO_ELIMINADO|id               borraron el grupo, quitalo de la interfaz
//    CANAL_ELIMINADO|id               borraron el canal, quitalo del catalogo
//    CUENTA_BORRADA|usuario           borra los mensajes y directos de ese usuario
//    CUENTA_ELIMINADA                 confirmacion pa quien borro su propia cuenta
//    DEL|ambito|clave|id              cambia el mensaje por un aviso de borrado
//    FOTO|usuario|perfil              perfil = "" / "animal:gato" / base64
// =====================================================================

#include <cstddef>
#include <string>
#include <vector>

const int PUERTO_POR_DEFECTO = 5000;

// La foto de perfil se achica en el cliente a FOTO_LADO x FOTO_LADO pixeles
// RGBA crudos (nada de PNG/JPG) y se manda en base64.
const int FOTO_LADO = 64;
const std::size_t FOTO_BYTES = static_cast<std::size_t>(FOTO_LADO) * FOTO_LADO * 4;
const std::size_t FOTO_B64_LONGITUD = ((FOTO_BYTES + 2) / 3) * 4;

// Avatares de default (las imagenes estan en recursos/avatares/<nombre>.png).
const int NUM_AVATARES = 5;
const char* const AVATARES_PREDETERMINADOS[NUM_AVATARES] = {"gato", "perro", "conejo", "oso", "zorro"};

inline bool esAvatarPredeterminado(const std::string& nombre) {
    for (const char* a : AVATARES_PREDETERMINADOS) {
        if (nombre == a) return true;
    }
    return false;
}

const std::size_t MAX_LINEA = 65536;   // bytes por linea del protocolo
const std::size_t MAX_TEXTO = 500;     // bytes por mensaje del chat
const std::size_t MAX_NOMBRE_USUARIO = 16;
const std::size_t MAX_NOMBRE_SALA = 24; // grupos y canales
const std::size_t MIN_CLAVE = 4;
const std::size_t MAX_CLAVE = 64;

// Parte "a|b|c|d" en maximo N campos; el ultimo se queda con TODO lo que
// sobra. Ejemplo: separarMaximo("M|G|x|hola | adios", 4) -> {"M","G","x","hola | adios"}
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
