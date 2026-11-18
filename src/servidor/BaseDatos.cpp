#include "BaseDatos.h"

#include <sqlite3.h>

#include <algorithm>
#include <iostream>

using namespace std;

// Version del esquema (se guarda en PRAGMA user_version).
static const int VERSION_ESQUEMA = 2;

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
    ejec("PRAGMA foreign_keys = ON"); // SQLite no hace cumplir las llaves foraneas si no se activa

    // ---- esquema viejo? (la primera version del proyecto usaba otras tablas) ----
    auto version = cons("PRAGMA user_version");
    int versionActual = version.empty() ? 0 : stoi(version[0][0]);
    bool hayTablas = !cons("SELECT name FROM sqlite_master WHERE type = 'table' AND name NOT LIKE 'sqlite_%'").empty();

    if (versionActual != VERSION_ESQUEMA && hayTablas) {
        cerr << "El archivo '" << ruta << "' tiene un esquema de una version anterior.\n"
             << "Borralo (rm " << ruta << ") o usa otro nombre de archivo para crear la base de datos nueva." << endl;
        return false;
    }

    // Tipos de datos estandar. Las fechas son DATETIME en texto "AAAA-MM-DD HH:MM:SS"
    // (hora local), asi se leen directo con un SELECT normal, sin convertir nada.
    const char* esquema =
        "CREATE TABLE IF NOT EXISTS Usuarios ("
        "  idUsuario INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  nombre VARCHAR(16) NOT NULL UNIQUE COLLATE NOCASE CHECK (length(nombre) BETWEEN 1 AND 16),"
        "  sal CHAR(32) NOT NULL,"
        "  hashClave CHAR(64) NOT NULL,"
        "  avatar VARCHAR(10) NOT NULL DEFAULT '' CHECK (avatar IN ('', 'foto', 'gato', 'perro', 'conejo', 'oso', 'zorro')),"
        "  foto TEXT NOT NULL DEFAULT '',"
        "  fechaRegistro DATETIME NOT NULL DEFAULT (datetime('now', 'localtime')));"

        "CREATE TABLE IF NOT EXISTS Grupos ("
        "  idGrupo INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  nombre VARCHAR(24) NOT NULL,"
        "  idDueno INTEGER NOT NULL,"
        "  fechaCreacion DATETIME NOT NULL DEFAULT (datetime('now', 'localtime')),"
        "  FOREIGN KEY (idDueno) REFERENCES Usuarios(idUsuario) ON DELETE CASCADE);"

        "CREATE TABLE IF NOT EXISTS GrupoMiembros ("
        "  idGrupo INTEGER NOT NULL,"
        "  idUsuario INTEGER NOT NULL,"
        "  fechaUnion DATETIME NOT NULL DEFAULT (datetime('now', 'localtime')),"
        "  PRIMARY KEY (idGrupo, idUsuario),"
        "  FOREIGN KEY (idGrupo) REFERENCES Grupos(idGrupo) ON DELETE CASCADE,"
        "  FOREIGN KEY (idUsuario) REFERENCES Usuarios(idUsuario) ON DELETE CASCADE);"

        "CREATE TABLE IF NOT EXISTS Canales ("
        "  idCanal INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  nombre VARCHAR(24) NOT NULL UNIQUE COLLATE NOCASE,"
        "  idDueno INTEGER NOT NULL,"
        "  fechaCreacion DATETIME NOT NULL DEFAULT (datetime('now', 'localtime')),"
        "  FOREIGN KEY (idDueno) REFERENCES Usuarios(idUsuario) ON DELETE CASCADE);"

        "CREATE TABLE IF NOT EXISTS CanalMiembros ("
        "  idCanal INTEGER NOT NULL,"
        "  idUsuario INTEGER NOT NULL,"
        "  fechaUnion DATETIME NOT NULL DEFAULT (datetime('now', 'localtime')),"
        "  PRIMARY KEY (idCanal, idUsuario),"
        "  FOREIGN KEY (idCanal) REFERENCES Canales(idCanal) ON DELETE CASCADE,"
        "  FOREIGN KEY (idUsuario) REFERENCES Usuarios(idUsuario) ON DELETE CASCADE);"

        // tipo: G = general, D = directo, R = grupo, C = canal. Cada tipo usa SOLO su llave.
        "CREATE TABLE IF NOT EXISTS Mensajes ("
        "  idMensaje INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  tipo CHAR(1) NOT NULL CHECK (tipo IN ('G', 'D', 'R', 'C')),"
        "  idAutor INTEGER NOT NULL,"
        "  idDestino INTEGER,"
        "  idGrupo INTEGER,"
        "  idCanal INTEGER,"
        "  texto VARCHAR(500) NOT NULL CHECK (length(texto) BETWEEN 1 AND 500),"
        "  fechaEnvio DATETIME NOT NULL DEFAULT (datetime('now', 'localtime')),"
        "  FOREIGN KEY (idAutor) REFERENCES Usuarios(idUsuario) ON DELETE CASCADE,"
        "  FOREIGN KEY (idDestino) REFERENCES Usuarios(idUsuario) ON DELETE CASCADE,"
        "  FOREIGN KEY (idGrupo) REFERENCES Grupos(idGrupo) ON DELETE CASCADE,"
        "  FOREIGN KEY (idCanal) REFERENCES Canales(idCanal) ON DELETE CASCADE,"
        "  CHECK ((tipo = 'G' AND idDestino IS NULL AND idGrupo IS NULL AND idCanal IS NULL) OR"
        "         (tipo = 'D' AND idDestino IS NOT NULL AND idGrupo IS NULL AND idCanal IS NULL) OR"
        "         (tipo = 'R' AND idGrupo IS NOT NULL AND idDestino IS NULL AND idCanal IS NULL) OR"
        "         (tipo = 'C' AND idCanal IS NOT NULL AND idDestino IS NULL AND idGrupo IS NULL)));"

        "CREATE INDEX IF NOT EXISTS idxMensajesSala ON Mensajes(tipo, idGrupo, idCanal, idMensaje);"
        "CREATE INDEX IF NOT EXISTS idxMensajesDirectos ON Mensajes(idAutor, idDestino);";

    char* error = nullptr;

    if (sqlite3_exec(db, esquema, nullptr, nullptr, &error) != SQLITE_OK) {
        cerr << "Error creando las tablas: " << (error ? error : "?") << endl;
        sqlite3_free(error);
        return false;
    }

    ejec("PRAGMA user_version = " + to_string(VERSION_ESQUEMA));
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

// Columnas: id, ambito, clave, autor, destino, texto, fecha (en ese orden).
static const char* SELECT_MENSAJE =
    "SELECT m.idMensaje, m.tipo, "
    "CASE m.tipo WHEN 'G' THEN 'general' WHEN 'R' THEN CAST(m.idGrupo AS TEXT) "
    "            WHEN 'C' THEN CAST(m.idCanal AS TEXT) ELSE '' END, "
    "ua.nombre, COALESCE(ud.nombre, ''), m.texto, m.fechaEnvio "
    "FROM Mensajes m "
    "JOIN Usuarios ua ON ua.idUsuario = m.idAutor "
    "LEFT JOIN Usuarios ud ON ud.idUsuario = m.idDestino ";

static MensajeGuardado filaAMensaje(const vector<string>& f) {
    MensajeGuardado m;
    m.id = stoll(f[0]);
    m.ambito = f[1];
    m.clave = f[2];
    m.autor = f[3];
    m.destino = f[4];
    m.texto = f[5];
    m.fecha = f[6];
    return m;
}

// ---------------------------------------------------------------------
// Usuarios
// ---------------------------------------------------------------------

bool BaseDatos::crearUsuario(const string& nombre, const string& sal, const string& hash,
                             const string& avatar, const string& foto) {
    lock_guard<mutex> g(mutexBD);
    return ejec("INSERT INTO Usuarios(nombre, sal, hashClave, avatar, foto) VALUES(?1, ?2, ?3, ?4, ?5)",
                {nombre, sal, hash, avatar, foto});
}

bool BaseDatos::credenciales(const string& nombre, string& nombreReal, string& sal, string& hash) {
    lock_guard<mutex> g(mutexBD);
    auto filas = cons("SELECT nombre, sal, hashClave FROM Usuarios WHERE nombre = ?1", {nombre});

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
    auto filas = cons("SELECT nombre FROM Usuarios WHERE nombre = ?1", {nombre});
    return filas.empty() ? "" : filas[0][0];
}

bool BaseDatos::perfil(const string& nombre, string& avatar, string& foto) {
    lock_guard<mutex> g(mutexBD);
    auto filas = cons("SELECT avatar, foto FROM Usuarios WHERE nombre = ?1", {nombre});

    if (filas.empty()) {
        return false;
    }

    avatar = filas[0][0];
    foto = filas[0][1];
    return true;
}

// La identidad se toma de la sesion autenticada; no de un nombre aportado por el cliente.
// Las llaves foraneas ON DELETE CASCADE eliminan salas propias, relaciones y mensajes.
bool BaseDatos::eliminarCuenta(const string& usuario) {
    lock_guard<mutex> g(mutexBD);
    return ejec("DELETE FROM Usuarios WHERE nombre = ?1", {usuario}) && sqlite3_changes(db) == 1;
}

// ---------------------------------------------------------------------
// Mensajes
// ---------------------------------------------------------------------

bool BaseDatos::guardarMensaje(MensajeGuardado& m) {
    lock_guard<mutex> g(mutexBD);

    // Un mensaje usa SOLO la llave que le toca (los demas quedan en NULL; ver el CHECK de la tabla).
    string idGrupo = (m.ambito == "R") ? m.clave : "";
    string idCanal = (m.ambito == "C") ? m.clave : "";

    long long id = 0;

    if (!ejec("INSERT INTO Mensajes(tipo, idAutor, idDestino, idGrupo, idCanal, texto) VALUES("
              "?1, (SELECT idUsuario FROM Usuarios WHERE nombre = ?2), "
              "(SELECT idUsuario FROM Usuarios WHERE nombre = ?3), "
              "CAST(NULLIF(?4, '') AS INTEGER), CAST(NULLIF(?5, '') AS INTEGER), ?6)",
              {m.ambito, m.autor, m.destino, idGrupo, idCanal, m.texto}, &id)) {
        return false;
    }

    auto filas = cons("SELECT fechaEnvio FROM Mensajes WHERE idMensaje = ?1", {to_string(id)});
    m.id = id;
    m.fecha = filas.empty() ? "" : filas[0][0];
    return true;
}

bool BaseDatos::mensaje(long long id, MensajeGuardado& salida) {
    lock_guard<mutex> g(mutexBD);
    auto filas = cons(string(SELECT_MENSAJE) + "WHERE m.idMensaje = ?1", {to_string(id)});

    if (filas.empty()) {
        return false;
    }

    salida = filaAMensaje(filas[0]);
    return true;
}

bool BaseDatos::borrarMensaje(long long id) {
    lock_guard<mutex> g(mutexBD);
    return ejec("DELETE FROM Mensajes WHERE idMensaje = ?1", {to_string(id)});
}

vector<MensajeGuardado> BaseDatos::historial(const string& ambito, const string& clave, int limite) {
    lock_guard<mutex> g(mutexBD);

    string donde;
    vector<string> params;

    if (ambito == "G") {
        donde = "WHERE m.tipo = 'G' ";
    } else if (ambito == "R") {
        donde = "WHERE m.tipo = 'R' AND m.idGrupo = CAST(?1 AS INTEGER) ";
        params = {clave};
    } else if (ambito == "C") {
        donde = "WHERE m.tipo = 'C' AND m.idCanal = CAST(?1 AS INTEGER) ";
        params = {clave};
    } else {
        return {};
    }

    auto filas = cons(string(SELECT_MENSAJE) + donde + "ORDER BY m.idMensaje DESC LIMIT " + to_string(limite), params);

    vector<MensajeGuardado> mensajes;
    for (const auto& f : filas) mensajes.push_back(filaAMensaje(f));
    reverse(mensajes.begin(), mensajes.end()); // del mas viejo al mas nuevo
    return mensajes;
}

vector<MensajeGuardado> BaseDatos::historialDirecto(const string& a, const string& b, int limite) {
    lock_guard<mutex> g(mutexBD);
    auto filas = cons(string(SELECT_MENSAJE) +
                      "WHERE m.tipo = 'D' AND ((ua.nombre = ?1 AND ud.nombre = ?2) OR "
                      "(ua.nombre = ?2 AND ud.nombre = ?1)) "
                      "ORDER BY m.idMensaje DESC LIMIT " + to_string(limite),
                      {a, b});

    vector<MensajeGuardado> mensajes;
    for (const auto& f : filas) mensajes.push_back(filaAMensaje(f));
    reverse(mensajes.begin(), mensajes.end());
    return mensajes;
}

vector<string> BaseDatos::companerosDirectos(const string& usuario) {
    lock_guard<mutex> g(mutexBD);
    auto filas = cons("SELECT DISTINCT CASE WHEN ua.nombre = ?1 THEN ud.nombre ELSE ua.nombre END "
                      "FROM Mensajes m "
                      "JOIN Usuarios ua ON ua.idUsuario = m.idAutor "
                      "JOIN Usuarios ud ON ud.idUsuario = m.idDestino "
                      "WHERE m.tipo = 'D' AND (ua.nombre = ?1 OR ud.nombre = ?1)",
                      {usuario});

    vector<string> nombres;
    for (const auto& f : filas) nombres.push_back(f[0]);
    return nombres;
}

// ---------------------------------------------------------------------
// Grupos
// ---------------------------------------------------------------------

vector<string> BaseDatos::miembrosDe(const string& sql, long long id) {
    auto filas = cons(sql, {to_string(id)});

    vector<string> nombres;
    for (const auto& f : filas) nombres.push_back(f[0]);
    return nombres;
}

long long BaseDatos::crearGrupo(const string& nombre, const string& dueno, const vector<string>& miembros) {
    lock_guard<mutex> g(mutexBD);

    if (!ejec("BEGIN IMMEDIATE")) {
        return -1;
    }

    long long id = -1;
    bool ok = ejec("INSERT INTO Grupos(nombre, idDueno) VALUES(?1, (SELECT idUsuario FROM Usuarios WHERE nombre = ?2))",
                   {nombre, dueno}, &id);

    for (size_t i = 0; ok && i < miembros.size(); i++) {
        ok = ejec("INSERT OR IGNORE INTO GrupoMiembros(idGrupo, idUsuario) "
                  "VALUES(?1, (SELECT idUsuario FROM Usuarios WHERE nombre = ?2))",
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
    auto filas = cons("SELECT g.idGrupo, g.nombre, u.nombre FROM Grupos g "
                      "JOIN Usuarios u ON u.idUsuario = g.idDueno WHERE g.idGrupo = ?1", {to_string(id)});

    if (filas.empty()) return false;

    salida.id = stoll(filas[0][0]);
    salida.nombre = filas[0][1];
    salida.dueno = filas[0][2];
    return true;
}

vector<SalaGuardada> BaseDatos::gruposDe(const string& usuario) {
    lock_guard<mutex> g(mutexBD);
    auto filas = cons("SELECT g.idGrupo, g.nombre, d.nombre FROM Grupos g "
                      "JOIN Usuarios d ON d.idUsuario = g.idDueno "
                      "JOIN GrupoMiembros gm ON gm.idGrupo = g.idGrupo "
                      "JOIN Usuarios u ON u.idUsuario = gm.idUsuario "
                      "WHERE u.nombre = ?1 ORDER BY g.idGrupo",
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

vector<string> BaseDatos::miembrosGrupo(long long id) {
    lock_guard<mutex> g(mutexBD);
    return miembrosDe("SELECT u.nombre FROM GrupoMiembros gm JOIN Usuarios u ON u.idUsuario = gm.idUsuario "
                      "WHERE gm.idGrupo = ?1 ORDER BY gm.rowid", id);
}

bool BaseDatos::esMiembroGrupo(long long id, const string& usuario) {
    lock_guard<mutex> g(mutexBD);
    return !cons("SELECT 1 FROM GrupoMiembros gm JOIN Usuarios u ON u.idUsuario = gm.idUsuario "
                 "WHERE gm.idGrupo = ?1 AND u.nombre = ?2", {to_string(id), usuario}).empty();
}

// El permiso se comprueba en la MISMA operacion SQL: no basta con el boton del cliente.
// sqlite3_changes distingue una insercion real de una invitacion duplicada.
bool BaseDatos::agregarMiembroGrupo(long long id, const string& dueno, const string& nuevo) {
    lock_guard<mutex> g(mutexBD);
    return ejec("INSERT OR IGNORE INTO GrupoMiembros(idGrupo, idUsuario) "
                "SELECT g.idGrupo, u.idUsuario FROM Grupos g "
                "JOIN Usuarios d ON d.idUsuario = g.idDueno "
                "JOIN Usuarios u ON u.nombre = ?3 "
                "WHERE g.idGrupo = ?1 AND d.nombre = ?2",
                {to_string(id), dueno, nuevo}) && sqlite3_changes(db) == 1;
}

bool BaseDatos::quitarMiembroGrupo(long long id, const string& solicitante, const string& miembro) {
    lock_guard<mutex> g(mutexBD);
    // El dueno no puede expulsarse, ni abandonar una sala de la cual es propietario.
    return ejec("DELETE FROM GrupoMiembros WHERE idGrupo = ?1 "
                "AND idUsuario = (SELECT idUsuario FROM Usuarios WHERE nombre = ?3) "
                "AND EXISTS (SELECT 1 FROM Grupos g "
                "JOIN Usuarios d ON d.idUsuario = g.idDueno "
                "WHERE g.idGrupo = ?1 AND (d.nombre = ?2 OR ?2 = ?3) AND d.nombre <> ?3)",
                {to_string(id), solicitante, miembro}) && sqlite3_changes(db) == 1;
}

bool BaseDatos::eliminarGrupo(long long id, const string& dueno) {
    lock_guard<mutex> g(mutexBD);
    return ejec("DELETE FROM Grupos WHERE idGrupo = ?1 AND "
                "idDueno = (SELECT idUsuario FROM Usuarios WHERE nombre = ?2)",
                {to_string(id), dueno}) && sqlite3_changes(db) == 1;
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
    bool ok = ejec("INSERT INTO Canales(nombre, idDueno) VALUES(?1, (SELECT idUsuario FROM Usuarios WHERE nombre = ?2))",
                   {nombre, dueno}, &id) &&
              ejec("INSERT INTO CanalMiembros(idCanal, idUsuario) VALUES(?1, (SELECT idUsuario FROM Usuarios WHERE nombre = ?2))",
                   {to_string(id), dueno});

    if (!ok) {
        ejec("ROLLBACK");
        return -1;
    }

    ejec("COMMIT");
    return id;
}

bool BaseDatos::canal(long long id, SalaGuardada& salida) {
    lock_guard<mutex> g(mutexBD);
    auto filas = cons("SELECT c.idCanal, c.nombre, u.nombre FROM Canales c "
                      "JOIN Usuarios u ON u.idUsuario = c.idDueno WHERE c.idCanal = ?1", {to_string(id)});

    if (filas.empty()) return false;

    salida.id = stoll(filas[0][0]);
    salida.nombre = filas[0][1];
    salida.dueno = filas[0][2];
    return true;
}

vector<SalaGuardada> BaseDatos::todosLosCanales() {
    lock_guard<mutex> g(mutexBD);
    auto filas = cons("SELECT c.idCanal, c.nombre, u.nombre FROM Canales c "
                      "JOIN Usuarios u ON u.idUsuario = c.idDueno ORDER BY c.idCanal");

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
    return ejec("INSERT OR IGNORE INTO CanalMiembros(idCanal, idUsuario) "
                "VALUES(?1, (SELECT idUsuario FROM Usuarios WHERE nombre = ?2))",
                {to_string(id), usuario});
}

vector<string> BaseDatos::miembrosCanal(long long id) {
    lock_guard<mutex> g(mutexBD);
    return miembrosDe("SELECT u.nombre FROM CanalMiembros cm JOIN Usuarios u ON u.idUsuario = cm.idUsuario "
                      "WHERE cm.idCanal = ?1 ORDER BY cm.rowid", id);
}

bool BaseDatos::esMiembroCanal(long long id, const string& usuario) {
    lock_guard<mutex> g(mutexBD);
    return !cons("SELECT 1 FROM CanalMiembros cm JOIN Usuarios u ON u.idUsuario = cm.idUsuario "
                 "WHERE cm.idCanal = ?1 AND u.nombre = ?2", {to_string(id), usuario}).empty();
}

// El propietario es el unico que puede borrar el canal y su historial.
bool BaseDatos::eliminarCanal(long long id, const string& dueno) {
    lock_guard<mutex> g(mutexBD);
    return ejec("DELETE FROM Canales WHERE idCanal = ?1 AND "
                "idDueno = (SELECT idUsuario FROM Usuarios WHERE nombre = ?2)",
                {to_string(id), dueno}) && sqlite3_changes(db) == 1;
}
