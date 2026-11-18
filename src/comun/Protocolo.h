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
//    REGISTRO|usuario|clave|perfil    crea la cuenta y entra. perfil = "" (sin foto),
//                                     "animal:gato" (avatar predeterminado) o base64 (foto propia)
//    LOGIN|usuario|clave              entra con una cuenta existente
//    GENERAL|texto                    mensaje al chat general
//    DM|destino|texto                 mensaje directo
//    GRUPO_NUEVO|nombre|m1,m2,...     crea un grupo con esos miembros
//    GMSG|idGrupo|texto               mensaje a un grupo
//    GRUPO_AGREGAR|idGrupo|usuario     el dueno agrega un miembro (tambien desconectado)
//    GRUPO_QUITAR|idGrupo|usuario      el dueno retira un miembro
//    GRUPO_SALIR|idGrupo               un integrante sale (no el dueno)
//    GRUPO_ELIMINAR|idGrupo            solo el dueno borra el grupo y su historial
//    CANAL_NUEVO|nombre               crea un canal (el creador es el dueño)
//    CANAL_UNIR|idCanal               se une a un canal
//    CANAL_MIEMBROS_PEDIR|idCanal      consulta privada, solo el dueno
//    CANAL_ELIMINAR|idCanal            solo el dueno borra el canal
//    CUENTA_ELIMINAR|contrasena        borra la cuenta propia tras revalidar clave
//    CMSG|idCanal|texto               mensaje a un canal (solo el dueño)
//    BORRAR|idMensaje                 elimina un mensaje PROPIO (para todos)
//    FOTO_PEDIR|usuario               pide la foto de perfil de alguien
//    SALIR
//
//  SERVIDOR -> CLIENTE
//    OK|usuario                       login/registro correcto
//    ERR|motivo                       login/registro fallido
//    AVISO|texto                      error o aviso de cualquier comando
//    USERS|a,b,c                      usuarios en linea
//    M|ambito|clave|id|timestampUnix|autor|texto  (segundos desde 1970, UTC;
//                                                 el cliente lo convierte a hora local)
//          ambito: G general, D directo, R grupo, C canal
//          clave : "general" / el OTRO usuario / id de grupo / id de canal
//    GRUPO|id|nombre|dueno|m1,m2,...
//    CANAL|id|nombre|dueno|unido(0/1)
//    CANAL_MIEMBROS_INICIO|id         empieza la lista privada de suscriptores
//    CANAL_MIEMBRO|id|usuario         un suscriptor (una linea por persona)
//    CANAL_MIEMBROS_FIN|id            termino la lista
//    MD|ambito|clave|id|timestampUnix|autor  constancia de mensaje borrado
//    GRUPO_QUITADO|id                 receptor expulsado o que abandono el grupo
//    GRUPO_ELIMINADO|id               grupo borrado, eliminarlo de la interfaz
//    CANAL_ELIMINADO|id               canal borrado, eliminarlo del catalogo
//    CUENTA_BORRADA|usuario           borrar mensajes y directos de ese usuario
//    CUENTA_ELIMINADA                 confirmacion a quien borro su propia cuenta
//    DEL|ambito|clave|id              reemplazar mensaje por aviso de borrado
//    FOTO|usuario|perfil              perfil = "" / "animal:gato" / base64
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

// Avatares predeterminados (imagenes en recursos/avatares/<nombre>.png).
const int NUM_AVATARES = 5;
const char* const AVATARES_PREDETERMINADOS[NUM_AVATARES] = {"gato", "perro", "conejo", "oso", "zorro"};

inline bool esAvatarPredeterminado(const std::string& nombre) {
    for (const char* a : AVATARES_PREDETERMINADOS) {
        if (nombre == a) return true;
    }
    return false;
}

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
