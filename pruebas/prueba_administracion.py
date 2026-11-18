"""Prueba de integracion de administracion de grupos, canales y cuentas.
Uso: python3 pruebas/prueba_administracion.py ./servidor [puerto]
Crea una BD temporal y nunca modifica chat-v2.db.
"""
import os, socket, sqlite3, subprocess, sys, tempfile, time, signal

SERVER = sys.argv[1] if len(sys.argv) > 1 else './servidor'
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 5092
DB = os.path.join(tempfile.mkdtemp(prefix='telenenas-admin-'), 'prueba.db')
srv = subprocess.Popen([SERVER, str(PORT), DB], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)

class Cliente:
    def __init__(self):
        self.s = socket.create_connection(('127.0.0.1', PORT), timeout=4)
        self.s.settimeout(2)
        self.buf = b''
        self.pending = []
    def send(self, texto):
        self.s.sendall((texto+'\n').encode())
    def recv(self, timeout=0.5):
        if self.pending: return self.pending.pop(0)
        end=time.monotonic()+timeout
        while time.monotonic()<end:
            if b'\n' in self.buf:
                line, self.buf = self.buf.split(b'\n', 1)
                return line.decode()
            self.s.settimeout(max(0.01,end-time.monotonic()))
            try: chunk=self.s.recv(65536)
            except socket.timeout: break
            if not chunk: return None
            self.buf+=chunk
        if b'\n' in self.buf:
            line, self.buf=self.buf.split(b'\n',1)
            return line.decode()
        return None
    def expect(self, prefix, timeout=3):
        end=time.monotonic()+timeout
        seen=[]
        while time.monotonic()<end:
            line=self.recv(0.2)
            if line is None: continue
            if line.startswith(prefix): return line
            seen.append(line)
        raise AssertionError(f'Esperaba {prefix!r}; recibio {seen}')
    def drain(self):
        out=[]
        while True:
            line=self.recv(0.05)
            if line is None: return out
            out.append(line)

ok=0
def check(condition, label):
    global ok
    assert condition,label
    ok+=1
    print('ok', label)

try:
    time.sleep(0.25)
    a,b,c=(Cliente(),Cliente(),Cliente())
    for client,name in ((a,'Ana'),(b,'Beto'),(c,'Carla')):
        client.send(f'REGISTRO|{name}|clave123|')
        check(client.expect('OK|')==f'OK|{name}',f'registro {name}')
    for client in (a,b,c): client.drain()

    a.send('GRUPO_NUEVO|Mi equipo|Beto')
    group=a.expect('GRUPO|')
    gid=group.split('|')[1]
    b.expect('GRUPO|')
    a.send(f'GMSG|{gid}|historial previo')
    a.expect(f'M|R|{gid}|')
    b.expect(f'M|R|{gid}|')

    b.send(f'GRUPO_AGREGAR|{gid}|Carla')
    check('Solo el creador' in b.expect('AVISO|'),'integrante no agrega usuarios')
    c.send(f'GRUPO_QUITAR|{gid}|Beto')
    check('Solo el creador' in c.expect('AVISO|'),'no miembro no puede quitar')

    a.send(f'GRUPO_AGREGAR|{gid}|Carla')
    check('Carla' in a.expect('GRUPO|'),'dueno agrega miembro')
    check('Carla' in b.expect('GRUPO|'),'miembro existente recibe cambio')
    check('Carla' in c.expect('GRUPO|'),'nuevo miembro recibe sala')
    check('historial previo' in c.expect(f'M|R|{gid}|'),'nuevo miembro recibe historial')
    a.expect('AVISO|')
    a.send(f'GRUPO_AGREGAR|{gid}|Carla')
    check('ya pertenece' in a.expect('AVISO|'),'no duplica miembros')
    c.send(f'GMSG|{gid}|hola desde carla')
    for client in (a,b,c): check('hola desde carla' in client.expect(f'M|R|{gid}|'),'miembro nuevo puede escribir')

    a.send(f'GRUPO_SALIR|{gid}')
    check('creador' in a.expect('AVISO|'),'creador no puede abandonar grupo')
    b.send(f'GRUPO_SALIR|{gid}')
    check(b.expect('GRUPO_QUITADO|')==f'GRUPO_QUITADO|{gid}','miembro puede salir')
    a.expect('GRUPO|'); c.expect('GRUPO|'); b.expect('AVISO|')
    b.send(f'GMSG|{gid}|no debo entrar')
    check('No perteneces' in b.expect('AVISO|'),'exmiembro no puede escribir')

    a.send(f'GRUPO_AGREGAR|{gid}|Beto')
    a.expect('GRUPO|'); c.expect('GRUPO|'); b.expect('GRUPO|')
    check('historial previo' in b.expect(f'M|R|{gid}|'),'miembro readmitido ve historial')
    a.expect('AVISO|')
    a.send(f'GRUPO_QUITAR|{gid}|Ana')
    check('dueno no puede' in a.expect('AVISO|'),'dueno no se autoexpulsa')
    a.send(f'GRUPO_QUITAR|{gid}|Carla')
    check(c.expect('GRUPO_QUITADO|')==f'GRUPO_QUITADO|{gid}','dueno expulsa miembro')
    a.expect('GRUPO|'); b.expect('GRUPO|'); a.expect('AVISO|')
    c.send(f'GMSG|{gid}|mensaje expulsada')
    check('No perteneces' in c.expect('AVISO|'),'expulsada no puede escribir')
    b.send(f'GRUPO_ELIMINAR|{gid}')
    check('Solo el creador' in b.expect('AVISO|'),'miembro no puede eliminar grupo')
    a.send(f'GRUPO_ELIMINAR|{gid}')
    check(a.expect('GRUPO_ELIMINADO|')==f'GRUPO_ELIMINADO|{gid}','dueno elimina grupo')
    check(b.expect('GRUPO_ELIMINADO|')==f'GRUPO_ELIMINADO|{gid}','miembro recibe eliminacion')
    a.expect('AVISO|')

    a.send('CANAL_NUEVO|Noticias')
    canal=a.expect('CANAL|'); cid=canal.split('|')[1]
    b.expect('CANAL|'); c.expect('CANAL|')
    b.send(f'CANAL_ELIMINAR|{cid}')
    check('Solo el creador' in b.expect('AVISO|'),'ajeno no elimina canal')
    a.send(f'CANAL_ELIMINAR|{cid}')
    for client in (a,b,c): check(client.expect('CANAL_ELIMINADO|')==f'CANAL_ELIMINADO|{cid}','todos son notificados')
    a.expect('AVISO|')

    a.send('GRUPO_NUEVO|Duena Ana|Beto')
    own=a.expect('GRUPO|').split('|')[1]
    b.expect('GRUPO|')
    a.send(f'GMSG|{own}|se borra al eliminar cuenta'); a.expect(f'M|R|{own}|'); b.expect(f'M|R|{own}|')
    a.send('CANAL_NUEVO|Avisos de Ana')
    own_c=a.expect('CANAL|').split('|')[1]
    b.expect('CANAL|'); c.expect('CANAL|')
    b.send('GRUPO_NUEVO|Grupo de Beto|Ana')
    other=b.expect('GRUPO|').split('|')[1]
    a.expect('GRUPO|')
    a.send('CUENTA_ELIMINAR|incorrecta')
    check('incorrecta' in a.expect('AVISO|').lower(),'clave incorrecta bloquea borrado')
    a.send('CUENTA_ELIMINAR|clave123')
    check(a.expect(f'GRUPO_ELIMINADO|{own}')==f'GRUPO_ELIMINADO|{own}','propia sala desaparece')
    check(b.expect(f'GRUPO_ELIMINADO|{own}')==f'GRUPO_ELIMINADO|{own}','miembros quedan notificados')
    for client in (a,b,c): check(client.expect(f'CANAL_ELIMINADO|{own_c}')==f'CANAL_ELIMINADO|{own_c}','su canal desaparece')
    for client in (a,b,c): check(client.expect('CUENTA_BORRADA|')=='CUENTA_BORRADA|Ana','cuenta borrada notificada')
    check(a.expect('CUENTA_ELIMINADA')=='CUENTA_ELIMINADA','usuario recibe confirmacion')
    con=sqlite3.connect(DB)
    con.execute('PRAGMA foreign_keys=ON')
    check(con.execute("SELECT count(*) FROM Usuarios WHERE nombre='Ana'").fetchone()[0]==0,'cuenta eliminada de SQLite')
    check(con.execute('SELECT count(*) FROM Grupos WHERE idGrupo=?',(int(own),)).fetchone()[0]==0,'grupos propios eliminados en cascada')
    check(con.execute('SELECT count(*) FROM Canales WHERE idCanal=?',(int(own_c),)).fetchone()[0]==0,'canales propios eliminados en cascada')
    check(con.execute('SELECT count(*) FROM Grupos WHERE idGrupo=?',(int(other),)).fetchone()[0]==1,'grupos de otros se conservan')
    check(con.execute('SELECT count(*) FROM Mensajes WHERE texto=?',('se borra al eliminar cuenta',)).fetchone()[0]==0,'historial de grupo eliminado en cascada')
    check(not con.execute('PRAGMA foreign_key_check').fetchall(),'llaves foraneas intactas')
    con.close()
    b.send(f'GMSG|{other}|el grupo ajeno sigue disponible')
    check('el grupo ajeno sigue disponible' in b.expect(f'M|R|{other}|'),'grupo de tercero funciona tras borrar cuenta')
    d=Cliente(); d.send('LOGIN|Ana|clave123')
    check(d.expect('ERR|').startswith('ERR|Usuario o contrasena'),'login eliminado ya no funciona')
    d.s.close()
    print(f'\n{ok} comprobaciones de administracion OK')
finally:
    try: srv.send_signal(signal.SIGINT)
    except ProcessLookupError: pass
    try: srv.communicate(timeout=5)
    except subprocess.TimeoutExpired: srv.kill(); srv.wait()
