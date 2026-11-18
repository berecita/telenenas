#!/usr/bin/env python3
"""Asigna claves en texto plano a cuentas antiguas de TELECHAT.

Usar con el servidor detenido, DESPUES de abrir una vez la BD con el
servidor actualizado (que migra el esquema v2 al v3 y crea un respaldo).

    python3 configurar_claves.py chat-v2.db
"""

import getpass
import os
import sqlite3
import sys


def main():
    if len(sys.argv) != 2:
        print('Uso: python3 configurar_claves.py RUTA_A_BASE_DE_DATOS.db')
        return 2
    ruta = sys.argv[1]
    if not os.path.isfile(ruta):
        print('No existe la base de datos:', ruta)
        return 2
    try:
        with sqlite3.connect(ruta) as con:
            version = con.execute('PRAGMA user_version').fetchone()[0]
            columnas = [fila[1] for fila in con.execute('PRAGMA table_info(Usuarios)')]
            if version != 3 or 'clave' not in columnas:
                print('La BD todavia no esta migrada. Inicia el servidor actualizado una vez '
                      'con ese archivo, detenlo y vuelve a ejecutar este script.')
                return 2
            pendientes = con.execute('SELECT nombre FROM Usuarios WHERE clave = \'\' ORDER BY nombre').fetchall()
            if pendientes:
                print('Cuentas antiguas pendientes:', ', '.join(x[0] for x in pendientes))
            else:
                print('No hay cuentas antiguas pendientes; tambien puedes cambiar una clave existente.')
            usuario = input('Nombre de la cuenta a configurar (vacio para cancelar): ').strip()
            if not usuario:
                return 0
            existe = con.execute('SELECT 1 FROM Usuarios WHERE nombre = ? COLLATE NOCASE', (usuario,)).fetchone()
            if not existe:
                print('Esa cuenta no existe.')
                return 1
            clave = getpass.getpass('Nueva contrasena: ')
            repetir = getpass.getpass('Repetir contrasena: ')
            if clave != repetir:
                print('Las contrasenas no coinciden.')
                return 1
            if not 4 <= len(clave.encode('utf-8')) <= 64 or any(x in clave for x in '|\r\n'):
                print('Se necesitan de 4 a 64 bytes y no se permiten | ni saltos de linea.')
                return 1
            con.execute('UPDATE Usuarios SET clave = ? WHERE nombre = ? COLLATE NOCASE', (clave, usuario))
            con.commit()
            print('Contrasena actualizada; se guardo el texto exacto sin hash ni codificacion.')
    except sqlite3.Error as error:
        print('Error SQLite:', error)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
