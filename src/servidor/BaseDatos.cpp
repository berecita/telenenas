#include "BaseDatos.h"
#include <algorithm>
#include <clocale>
#include <cstdlib>

#include <iostream>
#include <sqlext.h>

using namespace std;

namespace {
void diagnostico(SQLSMALLINT tipo, SQLHANDLE h) {
    SQLCHAR estado[6] = {}, mensaje[1024] = {};
    SQLINTEGER codigo;
    SQLSMALLINT longitud;
    if (SQL_SUCCEEDED(SQLGetDiagRec(tipo, h, 1, estado, &codigo, mensaje, sizeof mensaje, &longitud)))
        cerr << "SQL Server [" << estado << "] " << mensaje << endl;
}

class Consulta {
public:
    SQLHSTMT st = SQL_NULL_HSTMT;
    explicit Consulta(SQLHDBC db) {
        SQLAllocHandle(SQL_HANDLE_STMT, db, &st);
    }
    ~Consulta() {
        if (st) SQLFreeHandle(SQL_HANDLE_STMT, st);
    }
    bool correcta(SQLRETURN r) {
        if (SQL_SUCCEEDED(r)) return true;
        if (st) diagnostico(SQL_HANDLE_STMT, st);
        return false;
    }
    bool ejecutar(const string& sql, const vector<string>& params) {
        if (!st) return false;
        // ?1, ?2... se pueden repetir en un predicado. ODBC enlaza por aparicion.
        string comando;
        SQLUSMALLINT pos = 0;
        for (size_t i = 0; i < sql.size(); ++i) {
            comando += sql[i];
            if (sql[i] != '?') continue;
            if (++i >= sql.size() || sql[i] < '1' || sql[i] > '9') return false;
            size_t indice = sql[i] - '1';
            if (indice >= params.size()) return false;
            const auto& p = params[indice];
            SQLULEN ancho = p.size() > 4000 ? 0 : max<size_t>(p.size(), 1);
            if (!correcta(SQLBindParameter(st, ++pos, SQL_PARAM_INPUT, SQL_C_CHAR,
                SQL_WVARCHAR, ancho, 0, (SQLPOINTER)p.c_str(), p.size()+1, nullptr))) return false;
        }
        SQLRETURN r = SQLExecDirect(st, (SQLCHAR*)comando.c_str(), SQL_NTS);
        return r == SQL_NO_DATA || correcta(r);
    }
};

string valorODBC(const string& v) {
    string r = "{";
    for (char c : v) {
        r += c;
        if (c == '}') r += '}';
    }
    return r + '}';
}
}

BaseDatos::~BaseDatos() {
    if (db) {
        SQLDisconnect(db);
        SQLFreeHandle(SQL_HANDLE_DBC, db);
    }
    if (entorno) SQLFreeHandle(SQL_HANDLE_ENV, entorno);
}

bool BaseDatos::abrir(const string& base) {
    setlocale(LC_CTYPE, "C.UTF-8");
    const char* clave = getenv("TELENENAS_DB_PASSWORD");
    if (!clave || !*clave) {
        cerr << "Falta TELENENAS_DB_PASSWORD para la cuenta SQL telenenas." << endl;
        return false;
    }
    if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &entorno)) ||
        !SQL_SUCCEEDED(SQLSetEnvAttr(entorno, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0)) ||
        !SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_DBC, entorno, &db))) return false;
    string cadena = "DRIVER={ODBC Driver 18 for SQL Server};SERVER=localhost;"
                    "Encrypt=yes;TrustServerCertificate=yes;UID=telenenas;DATABASE=" +
                    valorODBC(base) + ";PWD=" + valorODBC(clave);
    if (!SQL_SUCCEEDED(SQLDriverConnect(db, nullptr, (SQLCHAR*)cadena.c_str(), SQL_NTS,
                                       nullptr, 0, nullptr, SQL_DRIVER_NOPROMPT))) {
        diagnostico(SQL_HANDLE_DBC, db);
        return false;
    }
    if (!ejec("SET NOCOUNT OFF")) return false;
    auto filas = cons("SELECT COUNT(*) FROM sys.tables WHERE schema_id=SCHEMA_ID('dbo') AND "
                      "name IN ('Usuarios','Grupos','GrupoMiembros','Canales','CanalMiembros',"
                      "'Mensajes','MensajesEliminados','Lecturas')");
    if (filas.empty() || filas.front().front() != "8") {
        cerr << "Comprueba la base y el acceso a las ocho tablas dbo." << endl;
        return false;
    }
    return true;
}

bool BaseDatos::ejec(const string& sql, const vector<string>& params, long long* ultimoId) {
    Consulta q(db);
    if (!q.ejecutar(sql, params)) return false;
    if (ultimoId) {
        SQLBIGINT id = 0;
        if (!q.correcta(SQLFetch(q.st)) ||
            !q.correcta(SQLGetData(q.st, 1, SQL_C_SBIGINT, &id, sizeof id, nullptr))) return false;
        *ultimoId = id;
    }
    SQLLEN n = 0;
    afectados = SQL_SUCCEEDED(SQLRowCount(q.st, &n)) ? n : 0;
    return true;
}

vector<BaseDatos::Fila> BaseDatos::cons(const string& sql, const vector<string>& params) {
    Consulta q(db);
    if (!q.ejecutar(sql, params)) return {};
    SQLSMALLINT columnas = 0;
    if (!q.correcta(SQLNumResultCols(q.st, &columnas))) return {};
    vector<Fila> filas;
    SQLRETURN r;
    while ((r = SQLFetch(q.st)) != SQL_NO_DATA) {
        if (!q.correcta(r)) return {};
        Fila fila;
        for (SQLSMALLINT i = 1; i <= columnas; ++i) {
            string s;
            do {
                char trozo[4096] = {};
                SQLLEN longitud;
                r = SQLGetData(q.st, i, SQL_C_CHAR, trozo, sizeof trozo, &longitud);
                if (r == SQL_NO_DATA) break;
                if (!q.correcta(r)) return {};
                if (longitud == SQL_NULL_DATA) break;
                s += trozo;
            } while (r == SQL_SUCCESS_WITH_INFO);
            fila.push_back(move(s));
        }
        filas.push_back(move(fila));
    }
    return filas;
}

// Columnas: id, ambito, clave, autor, destino, texto, fecha (en ese orden).
static const char* SELECT_MENSAJE =
    "SELECT m.idMensaje, m.tipo, "
    "CASE m.tipo WHEN 'G' THEN 'general' WHEN 'R' THEN CAST(m.idGrupo AS VARCHAR(20)) "
    "            WHEN 'C' THEN CAST(m.idCanal AS VARCHAR(20)) ELSE '' END, "
    "ua.nombre, COALESCE(ud.nombre, ''), m.texto, CONVERT(char(19), m.fechaEnvio, 120) "
    "FROM dbo.Mensajes m "
    "JOIN dbo.Usuarios ua ON ua.idUsuario = m.idAutor "
    "LEFT JOIN dbo.Usuarios ud ON ud.idUsuario = m.idDestino ";

// Recupera constancias de mensajes borrados sin recuperar su contenido.
static const char* SELECT_ELIMINADO =
    "SELECT m.idMensaje, m.tipo, "
    "CASE m.tipo WHEN 'G' THEN 'general' WHEN 'R' THEN CAST(m.idGrupo AS VARCHAR(20)) "
    "            WHEN 'C' THEN CAST(m.idCanal AS VARCHAR(20)) ELSE '' END, "
    "ua.nombre, COALESCE(ud.nombre, ''), '' AS texto, CONVERT(char(19), m.fechaEnvio, 120) "
    "FROM dbo.MensajesEliminados m "
    "JOIN dbo.Usuarios ua ON ua.idUsuario = m.idAutor "
    "LEFT JOIN dbo.Usuarios ud ON ud.idUsuario = m.idDestino ";

static void ordenarHistorial(vector<MensajeGuardado>& mensajes, int limite) {
    sort(mensajes.begin(), mensajes.end(), [](const MensajeGuardado& a, const MensajeGuardado& b) {
        return a.id < b.id;
    });
    if (limite > 0 && mensajes.size() > static_cast<size_t>(limite)) {
        mensajes.erase(mensajes.begin(), mensajes.end() - limite);
    }
}

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

bool BaseDatos::crearUsuario(const string& nombre, const string& clave,
                             const string& avatar, const string& foto) {
    lock_guard<mutex> g(mutexBD);
    // El texto introducido se almacena literalmente, sin sal ni funcion hash.
    return ejec("INSERT INTO dbo.Usuarios(nombre, clave, avatar, foto) VALUES(?1, ?2, ?3, ?4)",
                {nombre, clave, avatar, foto});
}

bool BaseDatos::credenciales(const string& nombre, string& nombreReal, string& clave) {
    lock_guard<mutex> g(mutexBD);
    auto filas = cons("SELECT nombre, clave FROM dbo.Usuarios WHERE nombre = ?1", {nombre});
    if (filas.empty()) return false;
    nombreReal = filas[0][0];
    clave = filas[0][1];
    return true;
}

bool BaseDatos::lecturasInicializadas(const string& usuario) {
    lock_guard<mutex> g(mutexBD);
    auto filas = cons("SELECT lecturasInicializadas FROM dbo.Usuarios WHERE nombre=?1", {usuario});
    return !filas.empty() && filas[0][0] == "1";
}

bool BaseDatos::finalizarLecturasIniciales(const string& usuario) {
    lock_guard<mutex> g(mutexBD);
    return ejec("UPDATE dbo.Usuarios SET lecturasInicializadas=1 WHERE nombre=?1", {usuario});
}

bool BaseDatos::marcarLeido(const string& usuario, const string& ambito,
                          const string& clave, long long id) {
    lock_guard<mutex> g(mutexBD);
    if (id < 0) return false;
    return ejec("MERGE dbo.Lecturas WITH (HOLDLOCK) AS destino "
                "USING (SELECT idUsuario, CAST(?2 AS char(1)) AS ambito, "
                "CAST(?3 AS nvarchar(64)) AS clave, CAST(?4 AS bigint) AS ultimoId "
                "FROM dbo.Usuarios WHERE nombre=?1) AS origen "
                "ON destino.idUsuario=origen.idUsuario AND destino.ambito=origen.ambito "
                "AND destino.clave=origen.clave "
                "WHEN MATCHED AND destino.ultimoId < origen.ultimoId THEN "
                "UPDATE SET ultimoId=origen.ultimoId "
                "WHEN NOT MATCHED THEN INSERT (idUsuario,ambito,clave,ultimoId) "
                "VALUES (origen.idUsuario,origen.ambito,origen.clave,origen.ultimoId);",
                {usuario, ambito, clave, to_string(id)});
}

vector<LecturaGuardada> BaseDatos::lecturasDe(const string& usuario) {
    lock_guard<mutex> g(mutexBD);
    vector<LecturaGuardada> resultado;
    for (const auto& f : cons("SELECT l.ambito, l.clave, l.ultimoId FROM dbo.Lecturas l "
                              "JOIN dbo.Usuarios u ON u.idUsuario=l.idUsuario WHERE u.nombre=?1", {usuario})) {
        resultado.push_back({f[0], f[1], stoll(f[2])});
    }
    return resultado;
}

long long BaseDatos::ultimoIdConversacion(const string& usuario, const string& ambito,
                                         const string& clave) {
    lock_guard<mutex> g(mutexBD);
    string condicion;
    vector<string> params;
    if (ambito == "G" && clave == "general") {
        condicion = "tipo='G'";
    } else if (ambito == "R" || ambito == "C") {
        condicion = string("tipo='") + ambito + "' AND " + (ambito == "R" ? "idGrupo" : "idCanal") + "=?1";
        params = {clave};
    } else if (ambito == "D") {
        condicion = "tipo='D' AND ((idAutor=(SELECT idUsuario FROM dbo.Usuarios WHERE nombre=?1) "
                    "AND idDestino=(SELECT idUsuario FROM dbo.Usuarios WHERE nombre=?2)) OR "
                    "(idAutor=(SELECT idUsuario FROM dbo.Usuarios WHERE nombre=?2) "
                    "AND idDestino=(SELECT idUsuario FROM dbo.Usuarios WHERE nombre=?1)))";
        params = {usuario, clave};
    } else {
        return 0;
    }
    auto filas = cons("SELECT MAX(idMensaje) FROM ("
                      "SELECT idMensaje FROM dbo.Mensajes WHERE " + condicion + " UNION ALL "
                      "SELECT idMensaje FROM dbo.MensajesEliminados WHERE " + condicion + ") AS historial", params);
    return (filas.empty() || filas[0][0].empty()) ? 0 : stoll(filas[0][0]);
}

string BaseDatos::nombreCanonico(const string& nombre) {
    lock_guard<mutex> g(mutexBD);
    auto filas = cons("SELECT nombre FROM dbo.Usuarios WHERE nombre = ?1", {nombre});
    return filas.empty() ? "" : filas[0][0];
}

bool BaseDatos::perfil(const string& nombre, string& avatar, string& foto) {
    lock_guard<mutex> g(mutexBD);
    auto filas = cons("SELECT avatar, foto FROM dbo.Usuarios WHERE nombre = ?1", {nombre});

    if (filas.empty()) {
        return false;
    }

    avatar = filas[0][0];
    foto = filas[0][1];
    return true;
}

// La identidad se toma de la sesion autenticada; no de un nombre aportado por el cliente.
// Primero se eliminan las referencias que no tienen cascada en SQL Server.
bool BaseDatos::eliminarCuenta(const string& usuario) {
    lock_guard<mutex> g(mutexBD);
    if (!ejec("BEGIN TRANSACTION")) return false;
    const string uid = "(SELECT idUsuario FROM dbo.Usuarios WHERE nombre=?1)";
    const string condicion = " WHERE idAutor=" + uid + " OR idDestino=" + uid +
        " OR idGrupo IN (SELECT idGrupo FROM dbo.Grupos WHERE idDueno=" + uid + ")" +
        " OR idCanal IN (SELECT idCanal FROM dbo.Canales WHERE idDueno=" + uid + ")";
    bool ok = ejec("DELETE FROM dbo.Mensajes" + condicion, {usuario}) &&
         ejec("DELETE FROM dbo.MensajesEliminados" + condicion, {usuario}) &&
         ejec("DELETE FROM dbo.GrupoMiembros WHERE idUsuario=" + uid, {usuario}) &&
         ejec("DELETE FROM dbo.CanalMiembros WHERE idUsuario=" + uid, {usuario}) &&
         ejec("DELETE FROM dbo.Usuarios WHERE nombre=?1", {usuario}) && afectados == 1;
    if (ok && ejec("COMMIT")) return true;
    ejec("IF @@TRANCOUNT > 0 ROLLBACK");
    return false;
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

    if (!ejec("INSERT INTO dbo.Mensajes(tipo, idAutor, idDestino, idGrupo, idCanal, texto) OUTPUT inserted.idMensaje VALUES("
              "?1, (SELECT idUsuario FROM dbo.Usuarios WHERE nombre = ?2), "
              "(SELECT idUsuario FROM dbo.Usuarios WHERE nombre = ?3), "
              "CAST(NULLIF(?4, '') AS INTEGER), CAST(NULLIF(?5, '') AS INTEGER), ?6)",
              {m.ambito, m.autor, m.destino, idGrupo, idCanal, m.texto}, &id)) {
        return false;
    }

    auto filas = cons("SELECT CONVERT(char(19), fechaEnvio, 120) FROM dbo.Mensajes WHERE idMensaje = ?1", {to_string(id)});
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
    if (!ejec("BEGIN TRANSACTION")) return false;

    // Copiar solo metadatos antes de borrar el texto. Ambas operaciones son atomicas.
    bool ok = ejec("INSERT INTO dbo.MensajesEliminados "
                   "(idMensaje, tipo, idAutor, idDestino, idGrupo, idCanal, fechaEnvio) "
                   "SELECT idMensaje, tipo, idAutor, idDestino, idGrupo, idCanal, fechaEnvio "
                   "FROM dbo.Mensajes WHERE idMensaje = ?1", {to_string(id)});
    ok = ok && afectados == 1;
    if (ok) {
        ok = ejec("DELETE FROM dbo.Mensajes WHERE idMensaje = ?1", {to_string(id)});
        ok = ok && afectados == 1;
    }
    if (ok) ok = ejec("COMMIT");
    if (!ok) ejec("IF @@TRANCOUNT > 0 ROLLBACK");
    return ok;
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

    // limite <= 0 significa historial completo, usado por los canales.
    string finalSQL = "ORDER BY m.idMensaje DESC";
    if (limite > 0) finalSQL += " OFFSET 0 ROWS FETCH NEXT " + to_string(limite) + " ROWS ONLY";
    auto filas = cons(string(SELECT_MENSAJE) + donde + finalSQL, params);
    auto borrados = cons(string(SELECT_ELIMINADO) + donde + finalSQL, params);

    vector<MensajeGuardado> mensajes;
    mensajes.reserve(filas.size() + borrados.size());
    for (const auto& f : filas) mensajes.push_back(filaAMensaje(f));
    for (const auto& f : borrados) {
        auto m = filaAMensaje(f);
        m.eliminado = true;
        mensajes.push_back(m);
    }
    ordenarHistorial(mensajes, limite);
    return mensajes;
}

vector<MensajeGuardado> BaseDatos::historialDirecto(const string& a, const string& b, int limite) {
    lock_guard<mutex> g(mutexBD);
    string donde = "WHERE m.tipo = 'D' AND ((ua.nombre = ?1 AND ud.nombre = ?2) OR "
                   "(ua.nombre = ?2 AND ud.nombre = ?1)) ";
    string finalSQL = "ORDER BY m.idMensaje DESC";
    if (limite > 0) finalSQL += " OFFSET 0 ROWS FETCH NEXT " + to_string(limite) + " ROWS ONLY";
    auto filas = cons(string(SELECT_MENSAJE) + donde + finalSQL, {a, b});
    auto borrados = cons(string(SELECT_ELIMINADO) + donde + finalSQL, {a, b});

    vector<MensajeGuardado> mensajes;
    mensajes.reserve(filas.size() + borrados.size());
    for (const auto& f : filas) mensajes.push_back(filaAMensaje(f));
    for (const auto& f : borrados) {
        auto m = filaAMensaje(f);
        m.eliminado = true;
        mensajes.push_back(m);
    }
    ordenarHistorial(mensajes, limite);
    return mensajes;
}

vector<string> BaseDatos::companerosDirectos(const string& usuario) {
    lock_guard<mutex> g(mutexBD);
    // Incluye conversaciones cuyos UNICOS mensajes fueron eliminados.
    auto filas = cons("SELECT DISTINCT CASE WHEN ua.nombre = ?1 THEN ud.nombre ELSE ua.nombre END "
                      "FROM (SELECT idAutor, idDestino FROM dbo.Mensajes WHERE tipo = 'D' "
                      "UNION ALL SELECT idAutor, idDestino FROM dbo.MensajesEliminados WHERE tipo = 'D') m "
                      "JOIN dbo.Usuarios ua ON ua.idUsuario = m.idAutor "
                      "JOIN dbo.Usuarios ud ON ud.idUsuario = m.idDestino "
                      "WHERE ua.nombre = ?1 OR ud.nombre = ?1",
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

    if (!ejec("BEGIN TRANSACTION")) {
        return -1;
    }

    long long id = -1;
    bool ok = ejec("INSERT INTO dbo.Grupos(nombre, idDueno) OUTPUT inserted.idGrupo VALUES(?1, (SELECT idUsuario FROM dbo.Usuarios WHERE nombre = ?2))",
                   {nombre, dueno}, &id);

    for (size_t i = 0; ok && i < miembros.size(); i++) {
        ok = ejec("INSERT INTO dbo.GrupoMiembros(idGrupo, idUsuario) "
                  "SELECT ?1, u.idUsuario FROM dbo.Usuarios u WHERE u.nombre=?2 "
                  "AND NOT EXISTS (SELECT 1 FROM dbo.GrupoMiembros WITH (UPDLOCK,HOLDLOCK) "
                  "WHERE idGrupo=?1 AND idUsuario=u.idUsuario)",
                  {to_string(id), miembros[i]});
    }

    if (!ok) {
        ejec("IF @@TRANCOUNT > 0 ROLLBACK");
        return -1;
    }

    if (ejec("COMMIT")) return id;
    ejec("IF @@TRANCOUNT > 0 ROLLBACK");
    return -1;
}

bool BaseDatos::grupo(long long id, SalaGuardada& salida) {
    lock_guard<mutex> g(mutexBD);
    auto filas = cons("SELECT g.idGrupo, g.nombre, u.nombre FROM dbo.Grupos g "
                      "JOIN dbo.Usuarios u ON u.idUsuario = g.idDueno WHERE g.idGrupo = ?1", {to_string(id)});

    if (filas.empty()) return false;

    salida.id = stoll(filas[0][0]);
    salida.nombre = filas[0][1];
    salida.dueno = filas[0][2];
    return true;
}

vector<SalaGuardada> BaseDatos::gruposDe(const string& usuario) {
    lock_guard<mutex> g(mutexBD);
    auto filas = cons("SELECT g.idGrupo, g.nombre, d.nombre FROM dbo.Grupos g "
                      "JOIN dbo.Usuarios d ON d.idUsuario = g.idDueno "
                      "JOIN dbo.GrupoMiembros gm ON gm.idGrupo = g.idGrupo "
                      "JOIN dbo.Usuarios u ON u.idUsuario = gm.idUsuario "
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
    return miembrosDe("SELECT u.nombre FROM dbo.GrupoMiembros gm JOIN dbo.Usuarios u ON u.idUsuario = gm.idUsuario "
                      "WHERE gm.idGrupo = ?1 ORDER BY gm.fechaUnion, gm.idUsuario", id);
}

bool BaseDatos::esMiembroGrupo(long long id, const string& usuario) {
    lock_guard<mutex> g(mutexBD);
    return !cons("SELECT 1 FROM dbo.GrupoMiembros gm JOIN dbo.Usuarios u ON u.idUsuario = gm.idUsuario "
                 "WHERE gm.idGrupo = ?1 AND u.nombre = ?2", {to_string(id), usuario}).empty();
}

// El permiso se comprueba en la MISMA operacion SQL: no basta con el boton del cliente.
// El contador de filas distingue una insercion real de una invitacion duplicada.
bool BaseDatos::agregarMiembroGrupo(long long id, const string& dueno, const string& nuevo) {
    lock_guard<mutex> g(mutexBD);
    return ejec("INSERT INTO dbo.GrupoMiembros(idGrupo, idUsuario) "
                "SELECT g.idGrupo, u.idUsuario FROM dbo.Grupos g "
                "JOIN dbo.Usuarios d ON d.idUsuario = g.idDueno "
                "JOIN dbo.Usuarios u ON u.nombre = ?3 "
                "WHERE g.idGrupo = ?1 AND d.nombre = ?2",
                {to_string(id), dueno, nuevo}) && afectados == 1;
}

bool BaseDatos::quitarMiembroGrupo(long long id, const string& solicitante, const string& miembro) {
    lock_guard<mutex> g(mutexBD);
    // El dueno no puede expulsarse, ni abandonar una sala de la cual es propietario.
    return ejec("DELETE FROM dbo.GrupoMiembros WHERE idGrupo = ?1 "
                "AND idUsuario = (SELECT idUsuario FROM dbo.Usuarios WHERE nombre = ?3) "
                "AND EXISTS (SELECT 1 FROM dbo.Grupos g "
                "JOIN dbo.Usuarios d ON d.idUsuario = g.idDueno "
                "WHERE g.idGrupo = ?1 AND (d.nombre = ?2 OR ?2 = ?3) AND d.nombre <> ?3)",
                {to_string(id), solicitante, miembro}) && afectados == 1;
}

bool BaseDatos::eliminarGrupo(long long id, const string& dueno) {
    lock_guard<mutex> g(mutexBD);
    if (!ejec("BEGIN TRANSACTION")) return false;
    const string sala = " WHERE idGrupo IN (SELECT idGrupo FROM dbo.Grupos WHERE idGrupo=?1 "
                        "AND idDueno=(SELECT idUsuario FROM dbo.Usuarios WHERE nombre=?2))";
    const vector<string> parametros = {to_string(id), dueno};
    bool ok = ejec("DELETE FROM dbo.Mensajes" + sala, parametros) &&
              ejec("DELETE FROM dbo.MensajesEliminados" + sala, parametros) &&
              ejec("DELETE FROM dbo.Grupos" + sala, parametros) && afectados == 1;
    if (ok && ejec("COMMIT")) return true;
    ejec("IF @@TRANCOUNT > 0 ROLLBACK");
    return false;
}

// ---------------------------------------------------------------------
// Canales
// ---------------------------------------------------------------------

long long BaseDatos::crearCanal(const string& nombre, const string& dueno) {
    lock_guard<mutex> g(mutexBD);

    if (!ejec("BEGIN TRANSACTION")) {
        return -1;
    }

    long long id = -1;

    // Si el nombre ya existe, UNIQUE hace fallar el INSERT.
    bool ok = ejec("INSERT INTO dbo.Canales(nombre, idDueno) OUTPUT inserted.idCanal VALUES(?1, (SELECT idUsuario FROM dbo.Usuarios WHERE nombre = ?2))",
                   {nombre, dueno}, &id) &&
              ejec("INSERT INTO dbo.CanalMiembros(idCanal, idUsuario) VALUES(?1, (SELECT idUsuario FROM dbo.Usuarios WHERE nombre = ?2))",
                   {to_string(id), dueno});

    if (!ok) {
        ejec("IF @@TRANCOUNT > 0 ROLLBACK");
        return -1;
    }

    if (ejec("COMMIT")) return id;
    ejec("IF @@TRANCOUNT > 0 ROLLBACK");
    return -1;
}

bool BaseDatos::canal(long long id, SalaGuardada& salida) {
    lock_guard<mutex> g(mutexBD);
    auto filas = cons("SELECT c.idCanal, c.nombre, u.nombre FROM dbo.Canales c "
                      "JOIN dbo.Usuarios u ON u.idUsuario = c.idDueno WHERE c.idCanal = ?1", {to_string(id)});

    if (filas.empty()) return false;

    salida.id = stoll(filas[0][0]);
    salida.nombre = filas[0][1];
    salida.dueno = filas[0][2];
    return true;
}

vector<SalaGuardada> BaseDatos::todosLosCanales() {
    lock_guard<mutex> g(mutexBD);
    auto filas = cons("SELECT c.idCanal, c.nombre, u.nombre FROM dbo.Canales c "
                      "JOIN dbo.Usuarios u ON u.idUsuario = c.idDueno ORDER BY c.idCanal");

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
    return ejec("INSERT INTO dbo.CanalMiembros(idCanal, idUsuario) "
                "SELECT ?1, u.idUsuario FROM dbo.Usuarios u WHERE u.nombre=?2 "
                "AND NOT EXISTS (SELECT 1 FROM dbo.CanalMiembros WITH (UPDLOCK,HOLDLOCK) "
                "WHERE idCanal=?1 AND idUsuario=u.idUsuario)",
                {to_string(id), usuario});
}

vector<string> BaseDatos::miembrosCanal(long long id) {
    lock_guard<mutex> g(mutexBD);
    return miembrosDe("SELECT u.nombre FROM dbo.CanalMiembros cm JOIN dbo.Usuarios u ON u.idUsuario = cm.idUsuario "
                      "WHERE cm.idCanal = ?1 ORDER BY cm.fechaUnion, cm.idUsuario", id);
}

bool BaseDatos::esMiembroCanal(long long id, const string& usuario) {
    lock_guard<mutex> g(mutexBD);
    return !cons("SELECT 1 FROM dbo.CanalMiembros cm JOIN dbo.Usuarios u ON u.idUsuario = cm.idUsuario "
                 "WHERE cm.idCanal = ?1 AND u.nombre = ?2", {to_string(id), usuario}).empty();
}

// El propietario es el unico que puede borrar el canal y su historial.
bool BaseDatos::eliminarCanal(long long id, const string& dueno) {
    lock_guard<mutex> g(mutexBD);
    if (!ejec("BEGIN TRANSACTION")) return false;
    const string sala = " WHERE idCanal IN (SELECT idCanal FROM dbo.Canales WHERE idCanal=?1 "
                        "AND idDueno=(SELECT idUsuario FROM dbo.Usuarios WHERE nombre=?2))";
    const vector<string> parametros = {to_string(id), dueno};
    bool ok = ejec("DELETE FROM dbo.Mensajes" + sala, parametros) &&
              ejec("DELETE FROM dbo.MensajesEliminados" + sala, parametros) &&
              ejec("DELETE FROM dbo.Canales" + sala, parametros) && afectados == 1;
    if (ok && ejec("COMMIT")) return true;
    ejec("IF @@TRANCOUNT > 0 ROLLBACK");
    return false;
}
