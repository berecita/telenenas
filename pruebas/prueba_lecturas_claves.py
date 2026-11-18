"""Pruebas de claves literales y de cursor de lectura entre sesiones.
Uso: python3 pruebas/prueba_lecturas_claves.py ./servidor [puerto]
"""
import os, sqlite3, socket, subprocess, sys, tempfile, time, signal, select

server=os.path.abspath(sys.argv[1] if len(sys.argv)>1 else './servidor')
port=int(sys.argv[2]) if len(sys.argv)>2 else 5138
folder=tempfile.mkdtemp(prefix='telechat-lecturas-')
db=os.path.join(folder,'datos.db')
server_proc=subprocess.Popen([server,str(port),db],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
checks=0

def check(ok,description):
    global checks
    assert ok, description
    checks+=1
    print('ok ',description)

class Cliente:
    def __init__(self):
        self.s=socket.create_connection(('127.0.0.1',port),3)
        self.s.setblocking(False)
        self.buf=b''
        self.pend=[]
    def send(self,cmd): self.s.sendall((cmd+'\n').encode('utf-8'))
    def line(self,wait=1.0):
        if self.pend: return self.pend.pop(0)
        end=time.monotonic()+wait
        while time.monotonic()<end:
            if b'\n' in self.buf:
                v,self.buf=self.buf.split(b'\n',1)
                return v.decode('utf-8')
            if select.select([self.s],[],[],max(0,end-time.monotonic()))[0]:
                data=self.s.recv(65536)
                if not data:return None
                self.buf+=data
        return None
    def until(self,prefix,wait=4):
        t=time.monotonic()+wait
        out=[]
        while time.monotonic()<t:
            l=self.line(0.15)
            if l is not None:
                if l.startswith(prefix):return l
                out.append(l)
        raise AssertionError((prefix,out[-8:]))
    def collect(self,wait=0.3):
        t=time.monotonic()+wait
        out=[]
        while time.monotonic()<t:
            l=self.line(0.05)
            if l is not None: out.append(l)
        return out
    def close(self): self.s.close()

try:
    time.sleep(0.3)
    a,b=Cliente(),Cliente()
    a.send('REGISTRO|Ana|miprueba123|')
    check(a.until('OK|')=='OK|Ana','registro de Ana')
    b.send('REGISTRO|Beto|otraprueba123|')
    check(b.until('OK|')=='OK|Beto','registro de Beto')
    a.collect(); b.collect()
    with sqlite3.connect(db) as con:
        clave=con.execute('SELECT clave FROM Usuarios WHERE nombre=?',('Ana',)).fetchone()[0]
        check(clave=='miprueba123','se almacena la clave literalmente')
        cols=[x[1] for x in con.execute('PRAGMA table_info(Usuarios)')]
        check('hashClave' not in cols and 'sal' not in cols,'sin columnas sal ni hash')
        check(con.execute('PRAGMA user_version').fetchone()[0]==3,'esquema v3')

    a.send('DM|Beto|mensaje_leido')
    first=a.until('M|D|Beto|'); b.until('M|D|Ana|')
    id1=int(first.split('|')[3])
    b.send(f'MARCAR_LEIDO|D|Ana|{id1}')
    time.sleep(.15)
    with sqlite3.connect(db) as con:
        v=con.execute("SELECT ultimoId FROM Lecturas l JOIN Usuarios u USING(idUsuario) "
                      "WHERE u.nombre='Beto' AND ambito='D' AND l.clave='Ana'").fetchone()
        check(v is not None and v[0]==id1,'apertura del DM guarda ultimo mensaje visto')
    b.send('MARCAR_LEIDO|D|Ana|99999999')
    time.sleep(.15)
    with sqlite3.connect(db) as con:
        v=con.execute("SELECT ultimoId FROM Lecturas l JOIN Usuarios u USING(idUsuario) "
                      "WHERE u.nombre='Beto' AND ambito='D' AND l.clave='Ana'").fetchone()[0]
        check(v==id1,'cursor no permite IDs futuros inventados')
    b.send('SALIR'); b.close(); time.sleep(.2); a.collect()
    a.send('DM|Beto|mensaje_nuevo_desconectado')
    second=a.until('M|D|Beto|')
    id2=int(second.split('|')[3])
    check(id2>id1,'nuevo DM con ID posterior al leido')
    b2=Cliente(); b2.send('LOGIN|Beto|otraprueba123')
    check(b2.until('OK|')=='OK|Beto','Beto reconecta')
    lectura=b2.until('LECTURA|D|Ana|')
    check(lectura==f'LECTURA|D|Ana|{id1}','cursor conservado entre sesiones')
    historial=b2.collect(.6)
    check(any(x.startswith(f'M|D|Ana|{id2}|') for x in historial),
          'historial incluye mensaje nuevo mientras estaba desconectado')
    b2.send(f'MARCAR_LEIDO|D|Ana|{id2}')
    time.sleep(.15)
    with sqlite3.connect(db) as con:
        actual=con.execute("SELECT ultimoId FROM Lecturas l JOIN Usuarios u USING(idUsuario) "
                           "WHERE u.nombre='Beto' AND ambito='D' AND l.clave='Ana'").fetchone()[0]
        check(actual==id2,'nuevo DM pasa a leido al verlo')
    b2.send('SALIR'); b2.close(); time.sleep(.2)
    b3=Cliente(); b3.send('LOGIN|Beto|otraprueba123')
    b3.until('OK|')
    check(b3.until('LECTURA|D|Ana|')==f'LECTURA|D|Ana|{id2}',
          'siguiente login no marca ese DM como nuevo')
    b3.send('SALIR'); b3.close()
    a.send('SALIR'); a.close()
finally:
    server_proc.send_signal(signal.SIGINT)
    try: server_proc.communicate(timeout=5)
    except subprocess.TimeoutExpired: server_proc.kill()

# Migracion de v2 conserva usuarios y crea respaldo, pero no recupera la clave.
legacy=os.path.join(folder,'vieja.db')
with sqlite3.connect(legacy) as con:
    con.execute('CREATE TABLE Usuarios (idUsuario INTEGER PRIMARY KEY AUTOINCREMENT, nombre VARCHAR(16) NOT NULL UNIQUE COLLATE NOCASE, sal CHAR(32) NOT NULL, hashClave CHAR(64) NOT NULL, avatar VARCHAR(10) NOT NULL DEFAULT "", foto TEXT NOT NULL DEFAULT "", fechaRegistro DATETIME NOT NULL DEFAULT (datetime(\'now\', \'localtime\')))')
    con.execute("INSERT INTO Usuarios(nombre,sal,hashClave) VALUES ('Anterior','abcdef','abcdef123')")
    con.execute('PRAGMA user_version=2')
    con.commit()
p=subprocess.Popen([server,str(port+1),legacy],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
time.sleep(.4)
p.send_signal(signal.SIGINT)
try: p.communicate(timeout=5)
except subprocess.TimeoutExpired: p.kill()
with sqlite3.connect(legacy) as con:
    cols=[x[1] for x in con.execute('PRAGMA table_info(Usuarios)')]
    check('clave' in cols and 'hashClave' not in cols and 'sal' not in cols, 'migracion elimina hash/sal sin eliminar cuentas')
    check(con.execute("SELECT clave FROM Usuarios WHERE nombre='Anterior'").fetchone()==('',),
          'las claves originales no recuperables quedan pendientes')
    check(con.execute('PRAGMA user_version').fetchone()[0]==3,'version migrada a 3')
check(os.path.isfile(legacy+'.respaldo_v2.db'),'se crea respaldo automatico antes de migrar')
print(f'{checks} comprobaciones de contrasenas y notificaciones OK')
