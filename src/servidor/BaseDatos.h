#ifndef BASEDATOS_H
#define BASEDATOS_H

#include <mutex>
#include <string>
#include <vector>

// ---------------------------------------------------------------------
// Capa de acceso a la base de datos (SQLite).
//
// TODO el SQL vive en BaseDatos.cpp; el resto del servidor solo usa estos
// metodos. Si el profe exige otro motor (MySQL, PostgreSQL...), basta con
// reescribir BaseDatos.cpp manteniendo esta misma interfaz.
//
// Es seguro llamarla desde varios hilos: cada metodo toma el mutex interno.
// ---------------------------------------------------------------------

struct sqlite3; // declaracion adelantada: asi este .h no necesita <sqlite3.h>

struct MensajeGuardado {
    long long id = 0;
    std::string ambito;   // G general, D directo, R grupo, C canal
    std::string clave;    // "general", id de grupo o id de canal ("" en directos)
    std::string autor;
    std::string destino;  // solo en directos
    std::string texto;
    std::string fecha;    // "AAAA-MM-DD HH:MM:SS" (hora local del servidor)
};

struct SalaGuardada {      // un grupo o un canal
    long long id = 0;
    std::string nombre;
    std::string dueno;
};

class BaseDatos {
private:
    sqlite3* db = nullptr;
    std::mutex mutexBD;

    using Fila = std::vector<std::string>;

    // Helpers SIN mutex (los metodos publicos ya lo tienen tomado).
    bool ejec(const std::string& sql, const std::vector<std::string>& params = {},
              long long* ultimoId = nullptr);
    std::vector<Fila> cons(const std::string& sql, const std::vector<std::string>& params = {});

    std::vector<std::string> miembrosDe(const std::string& sql, long long id);

public:
    ~BaseDatos();

    bool abrir(const std::string& ruta);

    // ---- usuarios ----
    // avatar: "" (ninguno), "foto" (foto propia, en `foto` en base64) o el nombre de un animal.
    bool crearUsuario(const std::string& nombre, const std::string& sal, const std::string& hash,
                      const std::string& avatar, const std::string& foto);
    // Regresa false si no existe. `nombreReal` trae las mayusculas con que se registro.
    bool credenciales(const std::string& nombre, std::string& nombreReal,
                      std::string& sal, std::string& hash);
    std::string nombreCanonico(const std::string& nombre); // "" si no existe
    bool perfil(const std::string& nombre, std::string& avatar, std::string& foto);
    bool eliminarCuenta(const std::string& usuario); // elimina tambien sus salas mediante CASCADE

    // ---- mensajes ----
    // Guarda el mensaje y rellena m.id y m.fecha (la fecha la pone la base de datos).
    bool guardarMensaje(MensajeGuardado& m);
    bool mensaje(long long id, MensajeGuardado& salida);
    bool borrarMensaje(long long id);
    std::vector<MensajeGuardado> historial(const std::string& ambito, const std::string& clave, int limite);
    std::vector<MensajeGuardado> historialDirecto(const std::string& a, const std::string& b, int limite);
    std::vector<std::string> companerosDirectos(const std::string& usuario);

    // ---- grupos ----
    long long crearGrupo(const std::string& nombre, const std::string& dueno,
                         const std::vector<std::string>& miembros); // -1 si falla
    bool grupo(long long id, SalaGuardada& salida);
    std::vector<SalaGuardada> gruposDe(const std::string& usuario);
    std::vector<std::string> miembrosGrupo(long long id);
    bool esMiembroGrupo(long long id, const std::string& usuario);
    bool agregarMiembroGrupo(long long id, const std::string& dueno, const std::string& nuevo);
    bool quitarMiembroGrupo(long long id, const std::string& solicitante, const std::string& miembro);
    bool eliminarGrupo(long long id, const std::string& dueno);

    // ---- canales ----
    long long crearCanal(const std::string& nombre, const std::string& dueno); // -1 si el nombre ya existe
    bool canal(long long id, SalaGuardada& salida);
    std::vector<SalaGuardada> todosLosCanales();
    bool unirCanal(long long id, const std::string& usuario);
    std::vector<std::string> miembrosCanal(long long id);
    bool esMiembroCanal(long long id, const std::string& usuario);
    bool eliminarCanal(long long id, const std::string& dueno);
};

#endif
