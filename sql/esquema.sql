-- Ejecutar con sqlcmd -d Telenenas -b -i sql/esquema.sql.
-- Las tablas existentes conservan sus filas. El login de la aplicación
-- ya debe tener un usuario en esta base con acceso de lectura y escritura.
SET XACT_ABORT ON;
IF DB_NAME() <> N'Telenenas'
    THROW 50001, 'Selecciona la base Telenenas antes de ejecutar el esquema.', 1;

BEGIN TRANSACTION;

IF OBJECT_ID(N'dbo.Usuarios', N'U') IS NULL
    CREATE TABLE dbo.Usuarios (
        idUsuario INT IDENTITY(1,1) PRIMARY KEY,
        nombre NVARCHAR(16) COLLATE Latin1_General_100_CI_AS NOT NULL UNIQUE
            CHECK (LEN(nombre) BETWEEN 1 AND 16),
        clave NVARCHAR(MAX) NOT NULL DEFAULT N'',
        lecturasInicializadas BIT NOT NULL DEFAULT 0,
        avatar NVARCHAR(10) NOT NULL DEFAULT N''
            CHECK (avatar IN (N'', N'foto', N'gato', N'perro', N'conejo', N'oso', N'zorro')),
        foto NVARCHAR(MAX) NOT NULL DEFAULT N'',
        fechaRegistro DATETIME2(0) NOT NULL DEFAULT SYSDATETIME()
    );

IF OBJECT_ID(N'dbo.Grupos', N'U') IS NULL
    CREATE TABLE dbo.Grupos (
        idGrupo INT IDENTITY(1,1) PRIMARY KEY,
        nombre NVARCHAR(24) NOT NULL,
        idDueno INT NOT NULL REFERENCES dbo.Usuarios(idUsuario) ON DELETE CASCADE,
        fechaCreacion DATETIME2(0) NOT NULL DEFAULT SYSDATETIME()
    );

IF OBJECT_ID(N'dbo.GrupoMiembros', N'U') IS NULL
    CREATE TABLE dbo.GrupoMiembros (
        idGrupo INT NOT NULL REFERENCES dbo.Grupos(idGrupo) ON DELETE CASCADE,
        idUsuario INT NOT NULL REFERENCES dbo.Usuarios(idUsuario),
        fechaUnion DATETIME2(0) NOT NULL DEFAULT SYSDATETIME(),
        PRIMARY KEY (idGrupo, idUsuario)
    );

IF OBJECT_ID(N'dbo.Canales', N'U') IS NULL
    CREATE TABLE dbo.Canales (
        idCanal INT IDENTITY(1,1) PRIMARY KEY,
        nombre NVARCHAR(24) COLLATE Latin1_General_100_CI_AS NOT NULL UNIQUE,
        idDueno INT NOT NULL REFERENCES dbo.Usuarios(idUsuario) ON DELETE CASCADE,
        fechaCreacion DATETIME2(0) NOT NULL DEFAULT SYSDATETIME()
    );

IF OBJECT_ID(N'dbo.CanalMiembros', N'U') IS NULL
    CREATE TABLE dbo.CanalMiembros (
        idCanal INT NOT NULL REFERENCES dbo.Canales(idCanal) ON DELETE CASCADE,
        idUsuario INT NOT NULL REFERENCES dbo.Usuarios(idUsuario),
        fechaUnion DATETIME2(0) NOT NULL DEFAULT SYSDATETIME(),
        PRIMARY KEY (idCanal, idUsuario)
    );

IF OBJECT_ID(N'dbo.Mensajes', N'U') IS NULL
    CREATE TABLE dbo.Mensajes (
        idMensaje BIGINT IDENTITY(1,1) PRIMARY KEY,
        tipo CHAR(1) NOT NULL,
        idAutor INT NOT NULL REFERENCES dbo.Usuarios(idUsuario),
        idDestino INT NULL REFERENCES dbo.Usuarios(idUsuario),
        idGrupo INT NULL REFERENCES dbo.Grupos(idGrupo),
        idCanal INT NULL REFERENCES dbo.Canales(idCanal),
        texto NVARCHAR(500) NOT NULL CHECK (DATALENGTH(texto) > 0),
        fechaEnvio DATETIME2(0) NOT NULL DEFAULT SYSDATETIME(),
        CONSTRAINT CK_Mensajes_Destino CHECK (
            (tipo='G' AND idDestino IS NULL AND idGrupo IS NULL AND idCanal IS NULL) OR
            (tipo='D' AND idDestino IS NOT NULL AND idGrupo IS NULL AND idCanal IS NULL) OR
            (tipo='R' AND idDestino IS NULL AND idGrupo IS NOT NULL AND idCanal IS NULL) OR
            (tipo='C' AND idDestino IS NULL AND idGrupo IS NULL AND idCanal IS NOT NULL)
        )
    );

IF OBJECT_ID(N'dbo.MensajesEliminados', N'U') IS NULL
    CREATE TABLE dbo.MensajesEliminados (
        idMensaje BIGINT PRIMARY KEY,
        tipo CHAR(1) NOT NULL CHECK (tipo IN ('G','D','R','C')),
        idAutor INT NOT NULL REFERENCES dbo.Usuarios(idUsuario),
        idDestino INT NULL REFERENCES dbo.Usuarios(idUsuario),
        idGrupo INT NULL REFERENCES dbo.Grupos(idGrupo),
        idCanal INT NULL REFERENCES dbo.Canales(idCanal),
        fechaEnvio DATETIME2(0) NOT NULL
    );

IF OBJECT_ID(N'dbo.Lecturas', N'U') IS NULL
    CREATE TABLE dbo.Lecturas (
        idUsuario INT NOT NULL REFERENCES dbo.Usuarios(idUsuario) ON DELETE CASCADE,
        ambito CHAR(1) NOT NULL CHECK (ambito IN ('G','D','R','C')),
        clave NVARCHAR(64) COLLATE Latin1_General_100_CI_AS NOT NULL,
        ultimoId BIGINT NOT NULL DEFAULT 0,
        PRIMARY KEY (idUsuario, ambito, clave)
    );

IF NOT EXISTS (SELECT 1 FROM sys.indexes WHERE object_id=OBJECT_ID(N'dbo.Mensajes') AND name=N'idxMensajesSala')
    CREATE INDEX idxMensajesSala ON dbo.Mensajes(tipo, idGrupo, idCanal, idMensaje);
IF NOT EXISTS (SELECT 1 FROM sys.indexes WHERE object_id=OBJECT_ID(N'dbo.Mensajes') AND name=N'idxMensajesDirectos')
    CREATE INDEX idxMensajesDirectos ON dbo.Mensajes(idAutor, idDestino);
IF NOT EXISTS (SELECT 1 FROM sys.indexes WHERE object_id=OBJECT_ID(N'dbo.MensajesEliminados') AND name=N'idxEliminadosSala')
    CREATE INDEX idxEliminadosSala ON dbo.MensajesEliminados(tipo, idGrupo, idCanal, idMensaje);

COMMIT;
GO
