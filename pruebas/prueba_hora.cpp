// Prueba de conversion de fecha local del servidor -> UNIX -> hora del cliente.
// Compilar: g++ -std=c++17 pruebas/prueba_hora.cpp -o /tmp/prueba_hora
// Ejecutar: /tmp/prueba_hora ; requiere zona horaria America/Mexico_City instalada.
#include "../src/comun/FechaHora.h"
#include <cassert>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string>

static void zona(const char* tz) {
#ifdef _WIN32
    _putenv_s("TZ", tz);
    _tzset();
#else
    setenv("TZ", tz, 1);
    tzset();
#endif
}

int main() {
    // Mismo momento: 12:15 UTC = 06:15 en Ciudad de Mexico (2026, UTC-6).
    zona("UTC");
    const std::string instante = fechaInstante("2026-11-18 12:15:00");
    assert(instante.size() == 10);

    zona("America/Mexico_City");
    // Se prueba el formateo independientemente de si la fecha elegida es 'hoy'.
    std::time_t t = static_cast<std::time_t>(std::stoll(instante));
    const std::tm mexico = *std::localtime(&t);
    assert(mexico.tm_hour == 6 && mexico.tm_min == 15);
    assert(mexico.tm_year + 1900 == 2026 && mexico.tm_mon + 1 == 11 && mexico.tm_mday == 18);
    const std::string mostrado = horaLocalCliente(instante);
    assert(mostrado.size() >= 5 && mostrado.substr(mostrado.size() - 5) == "06:15");

    zona("UTC");
    assert(fechaInstante("2026-11-18 12:15:00") == instante);
    zona("America/Mexico_City");
    assert(fechaInstante("2026-11-18 06:15:00") == instante);
    assert(horaLocalCliente("12:15") == "12:15"); // compatibilidad con servidor viejo

    // Cambio de día: 02:30 UTC es 20:30 del día anterior en México.
    zona("UTC");
    const std::string madrugada = fechaInstante("2026-11-19 02:30:00");
    zona("America/Mexico_City");
    std::time_t ts = static_cast<std::time_t>(std::stoll(madrugada));
    const std::tm anterior = *std::localtime(&ts);
    assert(anterior.tm_mday == 18 && anterior.tm_hour == 20 && anterior.tm_min == 30);
    const std::string mostradoAyer = horaLocalCliente(madrugada);
    assert(mostradoAyer.size() >= 5 && mostradoAyer.substr(mostradoAyer.size() - 5) == "20:30");

    std::cout << "Prueba de zonas horarias y cambio de dia OK\n";
    return 0;
}
