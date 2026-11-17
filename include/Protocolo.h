#ifndef PROTOCOLO_H
#define PROTOCOLO_H

#include <string>
#include <vector>

// ---------------------------------------------------------------------
// Funciones de bajo nivel para mandar/recibir UN mensaje completo por un
// socket TCP. Un mensaje = una linea de texto terminada en '\n'
// (igual que en upifood).
//
// Mensajes CLIENTE -> SERVIDOR
//   NICK|nombre            pide entrar al chat con ese nombre
//   MSG|texto              mensaje para todos
//   PRIV|destino|texto     mensaje privado
//   SALIR                  me desconecto
//
// Mensajes SERVIDOR -> CLIENTE
//   OK|mensaje             respuesta a NICK (entraste)
//   ERR|motivo             respuesta a NICK (nombre invalido / repetido)
//   MSG|hora|usuario|texto mensaje publico
//   PRIV|hora|origen|texto mensaje privado que me mandaron
//   PRIVOUT|hora|destino|texto  confirmacion de MI mensaje privado
//   SYS|texto              aviso del sistema (alguien entro / salio...)
//   USERS|a,b,c            lista de usuarios conectados
// ---------------------------------------------------------------------

bool enviarMensaje(int socket, const std::string& mensaje);
bool recibirMensaje(int socket, std::string& mensajeRecibido);

// Separa "a|b|c|d" en maximo N campos; el ultimo se queda con TODO lo que
// sobre (asi el texto de un mensaje puede traer '|' sin romper nada).
//   separarMaximo("MSG|12:30|ana|hola | adios", 4) -> {"MSG","12:30","ana","hola | adios"}
std::vector<std::string> separarMaximo(const std::string& mensaje, std::size_t maxCampos,
                                       char separador = '|');

// Se conecta a ip:puerto esperando maximo `milisegundos`. Regresa el socket
// o -1 si fallo (en `error` queda el motivo). Evita que la ventana se quede
// congelada minutos si la IP no responde.
int conectarConTimeout(const std::string& ip, int puerto, int milisegundos, std::string& error);

// Pone un tiempo maximo de espera a recv() (0 = esperar siempre).
void ponerTimeoutRecepcion(int socket, int milisegundos);

#endif

