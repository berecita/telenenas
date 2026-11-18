// Prueba de estado sin abrir ventanas. Necesita SFML o un stub compatible.
#include "../src/cliente/Chat.h"
#include <cassert>
#include <iostream>

#ifdef PRUEBA_SIN_SFML
// Para comprobacion interna en entornos sin SFML; no reemplaza la prueba visual.
Ventana::Ventana(const std::string& nombre, int ancho, int alto):
    fuenteCargada(false), titulo(nombre), ancho(ancho), alto(alto) {}
float Ventana::medirTexto(const std::string& s, unsigned int) {return float(s.size()*8);}
bool Avatares::necesitaPedir(const std::string&) {return false;}
void Avatares::recibir(const std::string&,const std::string&) {}
#endif

int main() {
    Ventana ventana("TEST",1000,640);
    Avatares avatares;
    Chat chat(ventana,avatares);
    chat.yo="Ana";
    chat.procesar("GRUPO|8|Grupo familiar|Beto|Ana,Beto");
    assert(!chat.grupos.at("8").nuevos); // cargar grupo no es recibir un mensaje
    chat.procesar("LECTURA|R|8|10");
    chat.procesar("M|R|8|9|1795018887|Beto|anterior");
    assert(!chat.grupos.at("8").nuevos); // historial ya visto
    chat.procesar("M|R|8|11|1795018887|Beto|nuevo");
    assert(chat.grupos.at("8").nuevos); // nuevo mensaje recibido
    chat.grupos.at("8").nuevos=false; // el usuario abrio el chat
    chat.procesar("M|R|8|12|1795018887|Ana|lo escribi yo");
    assert(!chat.grupos.at("8").nuevos); // no avisar por lo que enviamos
    chat.procesar("LECTURA|R|8|12");
    chat.procesar("M|R|8|13|1795018887|Beto|mas tarde");
    assert(chat.grupos.at("8").nuevos);

    chat.procesar("LECTURA|D|Beto|20");
    chat.procesar("M|D|Beto|19|1795018887|Beto|antiguo");
    assert(!chat.directos.at("Beto").nuevos);
    chat.procesar("M|D|Beto|21|1795018887|Beto|nuevo directo");
    assert(chat.directos.at("Beto").nuevos);

    chat.procesar("CANAL|5|Noticias|Beto|1");
    chat.procesar("LECTURA|C|5|30");
    chat.procesar("M|C|5|29|1795018887|Beto|antiguo");
    assert(!chat.canales.at("5").nuevos);
    chat.procesar("M|C|5|31|1795018887|Beto|nuevo en canal");
    assert(chat.canales.at("5").nuevos);
    std::cout << "Notificaciones cliente: grupo, directo y canal correctos\n";
}
