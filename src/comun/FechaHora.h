#ifndef TELECHAT_FECHA_HORA_H
#define TELECHAT_FECHA_HORA_H

#include <cstdio>
#include <ctime>
#include <string>

// La base de datos sigue guardando las fechas en la zona horaria del servidor.
// El protocolo de red usa segundos UNIX (instante absoluto, sin zona horaria).
inline std::string fechaInstante(const std::string& fecha) {
    int anio = 0, mes = 0, dia = 0, hora = 0, minuto = 0, segundo = 0;
    if (std::sscanf(fecha.c_str(), "%d-%d-%d %d:%d:%d",
                    &anio, &mes, &dia, &hora, &minuto, &segundo) != 6) return fecha;

    std::tm local = {};
    local.tm_year = anio - 1900;
    local.tm_mon = mes - 1;
    local.tm_mday = dia;
    local.tm_hour = hora;
    local.tm_min = minuto;
    local.tm_sec = segundo;
    local.tm_isdst = -1; // deja al sistema determinar el horario de verano
    const std::time_t instante = std::mktime(&local);
    if (instante == static_cast<std::time_t>(-1)) return fecha;
    return std::to_string(static_cast<long long>(instante));
}

// El cliente grafico (un solo hilo) presenta la hora de SU computadora.
// Si recibe el formato anterior HH:MM, lo conserva sin convertirlo.
inline std::string horaLocalCliente(const std::string& marca) {
    if (marca.size() < 10 || marca.find_first_not_of("0123456789") != std::string::npos)
        return marca;

    std::time_t instante;
    try {
        instante = static_cast<std::time_t>(std::stoll(marca));
    } catch (...) {
        return marca;
    }

    std::tm* convertida = std::localtime(&instante);
    if (!convertida) return marca;
    const std::tm local = *convertida;

    const std::time_t ahora = std::time(nullptr);
    const std::tm* hoy = std::localtime(&ahora);
    const bool esHoy = hoy && hoy->tm_year == local.tm_year && hoy->tm_yday == local.tm_yday;

    char texto[32];
    if (!std::strftime(texto, sizeof(texto), esHoy ? "%H:%M" : "%d/%m %H:%M", &local))
        return marca;
    return texto;
}

#endif // TELECHAT_FECHA_HORA_H
