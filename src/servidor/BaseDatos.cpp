#include "BaseDatos.h"

#include <sqlite3.h>

#include <algorithm>
#include <iostream>

using namespace std;

BaseDatos::~BaseDatos() {
    if (db != nullptr) {
        sqlite3_close(db);
    }
}

bool BaseDatos::abrir(const string& ruta) {
    lock_guard<mutex> g(mutexBD);

    if (sqlite3_open(ruta.c_str(), &db) != SQLITE_OK) {
        cerr << "No se pudo abrir la base de datos '" << ruta << "': " << sqlite3_errmsg(db) << endl;
        return false;
    }

    sqlite3_busy_timeout(db, 5000);

    const char* esquema =
        "CREATE TABLE IF NOT EXISTS usuarios("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  nombre TEXT NOT NULL UNIQUE COLLATE NOCASE,"
        "  sal TEXT NOT NULL, hash TEXT NOT NULL,"
        "  foto TEXT NOT NULL DEFAULT '',"
        "  creado INTEGER NOT NULL);"
        "CREATE TABLE IF NOT EXISTS mensajes("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  ambito TEXT NOT NULL,"
        "  clave TEXT NOT NULL DEFAULT '',"
        "  autor TEXT NOT NULL,"
        "  destino TEXT NOT NULL DEFAULT '',"
        "  texto TEXT NOT NULL,"
        "  fecha INTEGER NOT NULL);"
        "CREATE INDEX IF NOT EXISTS idx_mensajes_sala ON mensajes(ambito, clave, id);"
        "CREATE TABLE IF NOT EXISTS grupos("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  nombre TEXT NOT NULL, dueno TEXT NOT NULL);"
        "CREATE TABLE IF NOT EXISTS grupo_miembros("
        "  grupo_id INTEGER NOT NULL,"
        "  usuario TEXT NOT NULL COLLATE NOCASE,"
        "  PRIMARY KEY(grupo_id, usuario));"
        "CREATE TABLE IF NOT EXISTS canales("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  nombre TEXT NOT NULL UNIQUE COLLATE NOCASE,"
        "  dueno TEXT NOT NULL);"
        "CREATE TABLE IF NOT EXISTS canal_miembros("
        "  canal_id INTEGER NOT NULL,"
        "  usuario TEXT NOT NULL COLLATE NOCASE,"
        "  PRIMARY KEY(canal_id, usuario));";

    char* error = nullptr;

    if (sqlite3_exec(db, esquema, nullptr, nullptr, &error) != SQLITE_OK) {
        cerr << "Error creando las tablas: " << (error ? error : "?") << endl;
        sqlite3_free(error);
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------
// Helpers: siempre con consultas PREPARADAS (los valores se pasan como
// parametros, nunca pegados al texto del SQL) para evitar inyeccion SQL.
// ---------------------------------------------------------------------

bool BaseDatos::ejec(const string& sql, const vector<string>& params, long long* ultimoId) {
    sqlite3_stmt* st = nullptr;

    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) {
        cerr << "SQL invalido: " << sqlite3_errmsg(db) << endl;
        return false;
    }

    for (size_t i = 0; i < params.size(); i++) {
        sqlite3_bind_text(st, static_cast<int>(i + 1), params[i].c_str(),
                          static_cast<int>(params[i].size()), SQLITE_TRANSIENT);
    }

    int r = sqlite3_step(st);
    bool ok = (r == SQLITE_DONE || r == SQLITE_ROW);

    if (ok && ultimoId != nullptr) {
        *ultimoId = sqlite3_last_insert_rowid(db);
    }

    sqlite3_finalize(st);
    return ok;
}

vector<BaseDatos::Fila> BaseDatos::cons(const string& sql, const vector<string>& params) {
    vector<Fila> filas;
    sqlite3_stmt* st = nullptr;

    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) {
        cerr << "SQL invalido: " << sqlite3_errmsg(db) << endl;
        return filas;
    }

    for (size_t i = 0; i < params.size(); i++) {
        sqlite3_bind_text(st, static_cast<int>(i + 1), params[i].c_str(),
                          static_cast<int>(params[i].size()), SQLITE_TRANSIENT);
    }

    int columnas = sqlite3_column_count(st);

    while (sqlite3_step(st) == SQLITE_ROW) {
        Fila fila;

        for (int c = 0; c < columnas; c++) {
            const unsigned char* t = sqlite3_column_text(st, c);
            fila.push_back(t != nullptr ? reinterpret_cast<const char*>(t) : "");
        }

        filas.push_back(std::move(fila));
    }

    sqlite3_finalize(st);
    return filas;
}

static MensajeGuardado filaAMensaje(const vector<string>& f) {
    MensajeGuardado m;
    m.id = stoll(f[0]);
    m.ambito = f[1];
    m.clave = f[2];
    m.autor = f[3];
    m.destino = f[4];
    m.texto = f[5];
    m.fecha = stoll(f[6]);
    return m;
}

static const char* COLUMNAS_MENSAJE = "id, ambito, clave, autor, destino, texto, fecha";

// ---------------------------------------------------------------------
// Usuarios
// ---------------------------------------------------------------------

bool BaseDatos::crearUsuario(const string& nombre, const string& sal, const string& hash, const string& foto) {
    lock_guard<mutex> g(mutexBD);
    return ejec("INSERT INTO usuarios(nombre, sal, hash, foto, creado) VALUES(?1, ?2, ?3, ?4, strftime('%s','now'))",
                {nombre, sal, hash, foto});
}

bool BaseDatos::credenciales(const string& nombre, string& nombreReal, string& sal, string& hash) {
    lock_guard<mutex> g(mutexBD);
    auto filas = cons("SELECT nombre, sal, hash FROM usuarios WHERE nombre = ?1", {nombre});

    if (filas.empty()) {
        return false;
    }

    nombreReal = filas[0][0];
    sal = filas[0][1];
    hash = filas[0][2];
    return true;
}

string BaseDatos::nombreCanonico(const string& nombre) {
    lock_guard<mutex> g(mutexBD);
    auto filas = cons("SELECT nombre FROM usuarios WHERE nombre = ?1", {nombre});
    return filas.empty() ? "" : filas[0][0];
}

string BaseDatos::foto(const string& nombre) {
    lock_guard<mutex> g(mutexBD);
    auto filas = cons("SELECT foto FROM usuarios WHERE nombre = ?1", {nombre});
    return filas.empty() ? "" : filas[0][0];
}

// ---------------------------------------------------------------------
// Mensajes
// ---------------------------------------------------------------------

long long BaseDatos::guardarMensaje(const string& ambito, const string& clave, const string& autor,
                                    const string& destino, const string& texto, long long fecha) {
    lock_guard<mutex> g(mutexBD);
    long long id = -1;

    if (!ejec("INSERT INTO mensajes(ambito, clave, autor, destino, texto, fecha) VALUES(?1,?2,?3,?4,?5,?6)",
              {ambito, clave, autor, destino, texto, to_string(fecha)}, &id)) {
        return -1;
    }

    return id;
}

vector<MensajeGuardado> BaseDatos::historial(const string& ambito, const string& clave, int limite) {
    lock_guard<mutex> g(mutexBD);
    auto filas = cons(string("SELECT ") + COLUMNAS_MENSAJE +
                      " FROM mensajes WHERE ambito = ?1 AND clave = ?2 ORDER BY id DESC LIMIT " +
                      to_string(limite),
                      {ambito, clave});

    vector<MensajeGuardado> mensajes;
    for (const auto& f : filas) mensajes.push_back(filaAMensaje(f));
    reverse(mensajes.begin(), mensajes.end()); // del mas viejo al mas nuevo
    return mensajes;
}

vector<MensajeGuardado> BaseDatos::historialDirecto(const string& a, const string& b, int limite) {
    lock_guard<mutex> g(mutexBD);
    auto filas = cons(string("SELECT ") + COLUMNAS_MENSAJE +
                      " FROM mensajes WHERE ambito = 'D' AND "
                      "((autor = ?1 AND destino = ?2) OR (autor = ?2 AND destino = ?1)) "
                      "ORDER BY id DESC LIMIT " + to_string(limite),
                      {a, b});

    vector<MensajeGuardado> mensajes;
    for (const auto& f : filas) mensajes.push_back(filaAMensaje(f));
    reverse(mensajes.begin(), mensajes.end());
    return mensajes;
}

vector<string> BaseDatos::companerosDirectos(const string& usuario) {
    lock_guard<mutex> g(mutexBD);
    auto filas = cons("SELECT DISTINCT CASE WHEN autor = ?1 THEN destino ELSE autor END "
                      "FROM mensajes WHERE ambito = 'D' AND (autor = ?1 OR destino = ?1)",
                      {usuario});

    vector<string> nombres;
    for (const auto& f : filas) nombres.push_back(f[0]);
    return nombres;
}

// ---------------------------------------------------------------------
// Grupos
// ---------------------------------------------------------------------

long long BaseDatos::crearGrupo(const string& nombre, const string& dueno, const vector<string>& miembros) {
    lock_guard<mutex> g(mutexBD);

    if (!ejec("BEGIN IMMEDIATE")) {
        return -1;
    }

    long long id = -1;
    bool ok = ejec("INSERT INTO grupos(nombre, dueno) VALUES(?1, ?2)", {nombre, dueno}, &id);

    for (size_t i = 0; ok && i < miembros.size(); i++) {
        ok = ejec("INSERT OR IGNORE INTO grupo_miembros(grupo_id, usuario) VALUES(?1, ?2)",
                  {to_string(id), miembros[i]});
    }

    if (!ok) {
        ejec("ROLLBACK");
        return -1;
    }

    ejec("COMMIT");
    return id;
}

bool BaseDatos::grupo(long long id, SalaGuardada& salida) {
    lock_guard<mutex> g(mutexBD);
    auto filas = cons("SELECT id, nombre, dueno FROM grupos WHERE id = ?1", {to_string(id)});

    if (filas.empty()) return false;

    salida.id = stoll(filas[0][0]);
    salida.nombre = filas[0][1];
    salida.dueno = filas[0][2];
    return true;
}

vector<SalaGuardada> BaseDatos::gruposDe(const string& usuario) {
    lock_guard<mutex> g(mutexBD);
    auto filas = cons("SELECT g.id, g.nombre, g.dueno FROM grupos g "
                      "JOIN grupo_miembros m ON m.grupo_id = g.id WHERE m.usuario = ?1 ORDER BY g.id",
                      {usuario});

    vector<SalaGuardada> grupos;

    for (const auto& f : filas) {
        SalaGuardada s;
        s.id = stoll(f[0]);
        s.nombre = f[1];
        s.dueno = f[2];
        grupos.push_back(s);
    }

    return grupos;
}

vector<string> BaseDatos::miembrosDe(const string& tabla, const string& columna, long long id) {
    // `tabla` y `columna` son constantes internas (nunca vienen del usuario).
    auto filas = cons("SELECT usuario FROM " + tabla + " WHERE " + columna + " = ?1 ORDER BY rowid",
                      {to_string(id)});

    vector<string> nombres;
    for (const auto& f : filas) nombres.push_back(f[0]);
    return nombres;
}

vector<string> BaseDatos::miembrosGrupo(long long id) {
    lock_guard<mutex> g(mutexBD);
    return miembrosDe("grupo_miembros", "grupo_id", id);
}

bool BaseDatos::esMiembroGrupo(long long id, const string& usuario) {
    lock_guard<mutex> g(mutexBD);
    return !cons("SELECT 1 FROM grupo_miembros WHERE grupo_id = ?1 AND usuario = ?2",
                 {to_string(id), usuario}).empty();
}

// ---------------------------------------------------------------------
// Canales
// ---------------------------------------------------------------------

long long BaseDatos::crearCanal(const string& nombre, const string& dueno) {
    lock_guard<mutex> g(mutexBD);

    if (!ejec("BEGIN IMMEDIATE")) {
        return -1;
    }

    long long id = -1;

    // Si el nombre ya existe, UNIQUE hace fallar el INSERT.
    bool ok = ejec("INSERT INTO canales(nombre, dueno) VALUES(?1, ?2)", {nombre, dueno}, &id) &&
              ejec("INSERT INTO canal_miembros(canal_id, usuario) VALUES(?1, ?2)", {to_string(id), dueno});

    if (!ok) {
        ejec("ROLLBACK");
        return -1;
    }

    ejec("COMMIT");
    return id;
}

bool BaseDatos::canal(long long id, SalaGuardada& salida) {
    lock_guard<mutex> g(mutexBD);
    auto filas = cons("SELECT id, nombre, dueno FROM canales WHERE id = ?1", {to_string(id)});

    if (filas.empty()) return false;

    salida.id = stoll(filas[0][0]);
    salida.nombre = filas[0][1];
    salida.dueno = filas[0][2];
    return true;
}

vector<SalaGuardada> BaseDatos::todosLosCanales() {
    lock_guard<mutex> g(mutexBD);
    auto filas = cons("SELECT id, nombre, dueno FROM canales ORDER BY id");

    vector<SalaGuardada> canales;

    for (const auto& f : filas) {
        SalaGuardada s;
        s.id = stoll(f[0]);
        s.nombre = f[1];
        s.dueno = f[2];
        canales.push_back(s);
    }

    return canales;
}

bool BaseDatos::unirCanal(long long id, const string& usuario) {
    lock_guard<mutex> g(mutexBD);
    return ejec("INSERT OR IGNORE INTO canal_miembros(canal_id, usuario) VALUES(?1, ?2)",
                {to_string(id), usuario});
}

vector<string> BaseDatos::miembrosCanal(long long id) {
    lock_guard<mutex> g(mutexBD);
    return miembrosDe("canal_miembros", "canal_id", id);
}

bool BaseDatos::esMiembroCanal(long long id, const string& usuario) {
    lock_guard<mutex> g(mutexBD);
    return !cons("SELECT 1 FROM canal_miembros WHERE canal_id = ?1 AND usuario = ?2",
                 {to_string(id), usuario}).empty();
}

