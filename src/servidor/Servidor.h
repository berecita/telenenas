#ifndef SERVIDOR_H
#define SERVIDOR_H

#include "BaseDatos.h"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Una conexion de un cliente. Varios hilos pueden mandarle datos a la vez
// (el suyo y los de otros usuarios que le escriben), por eso `envio` es un
// mutex: evita que dos lineas se mezclen byte por byte en el socket.
struct Sesion {
    int fd = -1;
    std::string usuario;       // vacio hasta que inicia sesion
    std::string entrada;       // bytes recibidos que aun no forman una linea completa
    std::mutex envio;
    std::atomic<bool> activa{true};
};

using SesionPtr = std::shared_ptr<Sesion>;

// Se puede llamar desde un manejador de senales (Ctrl+C).
void solicitarApagado();

class Servidor {
private:
    int puerto;
    std::string rutaBD;
    BaseDatos bd;

    std::map<std::string, SesionPtr> enLinea; // clave: nombre en minusculas
    std::mutex mutexEnLinea;

    // ---- envio ----
    bool enviar(const SesionPtr& s, const std::string& mensaje);
    void enviarA(const std::string& usuario, const std::string& mensaje);
    void enviarAVarios(const std::vector<std::string>& usuarios, const std::string& mensaje);
    void difundir(const std::string& mensaje);
    void difundirUsuarios();

    // ---- ciclo de vida de una conexion ----
    void atender(SesionPtr s);
    bool autenticar(const SesionPtr& s);
    void enviarEstadoInicial(const SesionPtr& s);
    void procesarComando(const SesionPtr& s, const std::string& linea, bool& salir);

    // ---- comandos ----
    void cmdGeneral(const SesionPtr& s, const std::string& texto);
    void cmdDirecto(const SesionPtr& s, const std::string& destino, const std::string& texto);
    void cmdGrupoNuevo(const SesionPtr& s, const std::string& nombre, const std::string& miembros);
    void cmdGrupoMensaje(const SesionPtr& s, const std::string& id, const std::string& texto);
    void cmdCanalNuevo(const SesionPtr& s, const std::string& nombre);
    void cmdCanalUnir(const SesionPtr& s, const std::string& id);
    void cmdCanalMensaje(const SesionPtr& s, const std::string& id, const std::string& texto);
    void cmdBorrar(const SesionPtr& s, const std::string& id);
    void cmdFoto(const SesionPtr& s, const std::string& usuario);

public:
    Servidor(int puerto, const std::string& rutaBD);

    // Bloquea hasta que llega Ctrl+C. Regresa false si no pudo arrancar.
    bool iniciar();
};

#endif
