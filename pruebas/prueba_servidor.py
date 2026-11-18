"""Prueba de integracion del servidor: levanta un servidor temporal y lo usa como
lo haria el cliente (registro, login, general, directos, grupos, canales, fotos,
historial, inyeccion SQL...).

Uso:  python3 pruebas/prueba_servidor.py ./build/servidor  [puerto]
"""
import socket, time, base64, sqlite3, os, subprocess, sys, signal, tempfile

if len(sys.argv) < 2:
    sys.exit(__doc__)

PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 5077
DB = os.path.join(tempfile.mkdtemp(), 'prueba.db')
srv = subprocess.Popen([sys.argv[1], str(PORT), DB], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
time.sleep(0.6)

class C:
    def __init__(s):
        s.s=socket.create_connection(('127.0.0.1',PORT)); s.s.settimeout(2.0); s.buf=b''
    def send(s,l): s.s.sendall(l.encode()+b'\n')
    def line(s,timeout=2.0):
        s.s.settimeout(timeout)
        while b'\n' not in s.buf:
            try: d=s.s.recv(65536)
            except socket.timeout: return None
            if not d: return None
            s.buf+=d
        l,s.buf=s.buf.split(b'\n',1); return l.decode()
    def drain(s,t=0.4):
        out=[]
        while True:
            l=s.line(t)
            if l is None: return out
            out.append(l)
    def expect(s,prefix,t=2.0):
        end=time.time()+t; seen=[]
        while time.time()<end:
            l=s.line(0.3)
            if l is None: continue
            seen.append(l)
            if l.startswith(prefix): return l
        raise AssertionError(f"esperaba {prefix!r}, llegó {seen}")

ok=0
def check(c,msg):
    global ok
    if not c: print("FALLA:",msg); cleanup(); sys.exit(1)
    ok+=1; print("ok  ",msg)
def cleanup():
    srv.send_signal(signal.SIGINT)
    try: print(srv.communicate(timeout=6)[0][-600:])
    except Exception: srv.kill()

foto=base64.b64encode(bytes((i*7)%256 for i in range(64*64*4))).decode()
check(len(foto)==21848,"foto de prueba mide 21848")

# --- registro / login: errores ---
a=C()
a.send("REGISTRO|ana|ab|"); check(a.expect("ERR").startswith("ERR|La contrasena"),"clave corta rechazada")
a.send("REGISTRO|a,b|abcd|"); check("no puede llevar" in a.expect("ERR"),"nombre con coma rechazado")
a.send("REGISTRO|ana|abcd|AAAA"); check("foto" in a.expect("ERR"),"foto invalida rechazada")
a.send("LOGIN|nadie|abcd"); check(a.expect("ERR")=="ERR|Usuario o contrasena incorrectos.","login inexistente")
a.send("GENERAL|hola"); check(a.expect("ERR").startswith("ERR|Primero"),"sin login no se puede chatear")
a.send(f"REGISTRO|Ana|clave123|{foto}"); check(a.expect("OK")=="OK|Ana","registro con foto")
a.drain()
bad=C(); bad.send("REGISTRO|dragon|abcd1234|animal:dragon"); check("avatar" in bad.expect("ERR"),"avatar inexistente rechazado"); bad.s.close()
b=C(); b.send("REGISTRO|beto|secreto|animal:gato"); check(b.expect("OK")=="OK|beto","registro con avatar predeterminado"); b.drain(); a.drain()
c2=C(); c2.send("REGISTRO|ANA|otraclave|"); check("ya existe" in c2.expect("ERR"),"usuario repetido (sin importar mayusculas)")
c2.send("LOGIN|ana|mala"); check("incorrectos" in c2.expect("ERR"),"clave incorrecta")
c2.send("LOGIN|ana|clave123"); check("sesion abierta" in c2.expect("ERR"),"doble sesion rechazada")
c2.send("REGISTRO|cesar|cesar123|"); c2.expect("OK"); c2.drain(); a.drain(); b.drain()

# --- general ---
a.send("GENERAL|hola a todos | con barra")
la=a.expect("M|G"); lb=b.expect("M|G"); lc=c2.expect("M|G")
check(la==lb==lc and la.endswith("|Ana|hola a todos | con barra"),"general llega a todos (con '|' en el texto)")

# --- directos ---
a.send("DM|BETO|secreto de ana")
check(a.expect("M|D").startswith("M|D|beto|"),"DM: el emisor lo ve bajo el nombre del destino")
check(b.expect("M|D").startswith("M|D|Ana|"),"DM: el receptor lo ve bajo el nombre del emisor")
check(c2.line(0.5) is None or True,"")
a.send("DM|nadie|x"); check("no existe" in a.expect("AVISO"),"DM a usuario inexistente")
a.send("DM|ana|x"); check("a ti mismo" in a.expect("AVISO"),"DM a uno mismo")
cl=c2.drain(0.3); check(not any("secreto" in l for l in cl),"cesar NO ve el DM")

# --- grupos ---
a.send("GRUPO_NUEVO|Equipo|beto,fantasma,ana"); g=a.expect("GRUPO"); gid=g.split('|')[1]
check(g.endswith("|Equipo|Ana|Ana,beto"),"grupo creado (ignora inexistentes y duplicados): "+g); b.expect("GRUPO")
b.send(f"GMSG|{gid}|hola grupo"); check(a.expect("M|R").startswith(f"M|R|{gid}|"),"mensaje de grupo llega a la dueña"); b.expect("M|R")
c2.send(f"GMSG|{gid}|intruso"); check("No perteneces" in c2.expect("AVISO"),"no miembro no puede escribir en el grupo")
check(not any("intruso" in l for l in a.drain(0.3)),"el intruso no llegó al grupo")

# --- canales ---
a.send("CANAL_NUEVO|Avisos"); ca=a.expect("CANAL"); cid=ca.split('|')[1]
check(ca.endswith("|Avisos|Ana|1"),"canal creado, dueña unida"); check(b.expect("CANAL").endswith("|Avisos|Ana|0"),"otros lo ven sin estar unidos")
b.send("CANAL_NUEVO|avisos"); check("Ya existe" in b.expect("AVISO"),"nombre de canal repetido (sin importar mayusculas)")
a.send(f"CMSG|{cid}|primer aviso")
check(a.expect("M|C").endswith("|Ana|primer aviso"),"el dueño escribe en su canal")
check(not any("primer aviso" in l for l in b.drain(0.3)),"quien no se unió no lo recibe en vivo")
b.send(f"CANAL_UNIR|{cid}"); check(b.expect("CANAL").endswith("|1"),"beto se une")
check(b.expect("M|C").endswith("|primer aviso"),"al unirse ve el historial del canal")
b.send(f"CMSG|{cid}|yo tambien"); check("Solo el dueno" in b.expect("AVISO"),"quien se une NO puede escribir")
a.send(f"CMSG|{cid}|segundo aviso"); check(b.expect("M|C").endswith("|segundo aviso"),"el miembro recibe lo nuevo")

# --- fotos ---
b.send("FOTO_PEDIR|ana"); check(b.expect("FOTO")==f"FOTO|Ana|{foto}","la foto llega íntegra")
a.send("FOTO_PEDIR|beto"); check(a.expect("FOTO")=="FOTO|beto|animal:gato","avatar predeterminado se devuelve como animal:gato")
a.send("FOTO_PEDIR|cesar"); check(a.expect("FOTO")=="FOTO|cesar|","usuario sin foto => vacío")

# --- borrar mensajes ---
a.drain(); b.drain(); c2.drain()
a.send("GENERAL|mensaje para borrar"); l=a.expect("M|G"); gid_msg=l.split('|')[3]; b.expect("M|G"); c2.expect("M|G")
b.send(f"BORRAR|{gid_msg}"); check("Solo puedes" in b.expect("AVISO"),"no se puede borrar el mensaje de otro")
a.send(f"BORRAR|{gid_msg}")
check(a.expect("DEL")==f"DEL|G|general|{gid_msg}" and b.expect("DEL")==f"DEL|G|general|{gid_msg}" and c2.expect("DEL")==f"DEL|G|general|{gid_msg}","borrar en general avisa a todos")
a.send(f"BORRAR|{gid_msg}"); check("ya no existe" in a.expect("AVISO"),"borrar dos veces => aviso")
a.send("BORRAR|abc"); check("ya no existe" in a.expect("AVISO"),"id invalido => aviso")
a.send("DM|beto|dm para borrar"); l=a.expect("M|D"); did=l.split('|')[3]; b.expect("M|D")
a.send(f"BORRAR|{did}"); check(a.expect("DEL")==f"DEL|D|beto|{did}" and b.expect("DEL")==f"DEL|D|Ana|{did}","borrar un directo avisa a ambos (cada quien con su clave)")
b.send(f"GMSG|{gid}|grupo para borrar"); l=b.expect("M|R"); rid=l.split('|')[3]; a.expect("M|R")
a.send(f"BORRAR|{rid}"); check("Solo puedes" in a.expect("AVISO"),"el dueño del grupo tampoco borra mensajes ajenos")
b.send(f"BORRAR|{rid}"); check(a.expect("DEL")==f"DEL|R|{gid}|{rid}" and b.expect("DEL")==f"DEL|R|{gid}|{rid}","borrar en grupo avisa a los miembros")
a.send(f"CMSG|{cid}|canal para borrar"); l=a.expect("M|C"); kid=l.split('|')[3]; b.expect("M|C")
a.send(f"BORRAR|{kid}"); check(a.expect("DEL")==f"DEL|C|{cid}|{kid}" and b.expect("DEL")==f"DEL|C|{cid}|{kid}","borrar en canal avisa a los miembros")
c2.drain()

# --- usuarios en linea y salida ---
a.drain(); b.drain(); c2.drain()
c2.send("SALIR"); time.sleep(0.4)
check(any(l=="USERS|Ana,beto" for l in a.drain(0.5)),"lista de usuarios se actualiza al salir cesar")

# --- inyeccion SQL ---
d=C(); d.send("REGISTRO|x'; DROP TABLE--|pass1234|"); d.expect("OK"); d.drain()
d.send("DM|beto' OR '1'='1|hola"); check("no existe" in d.expect("AVISO"),"inyeccion en DM no hace nada")
d.send("GENERAL|'); DROP TABLE mensajes;--"); d.expect("M|G"); d.send("SALIR")

# --- historial al volver a entrar ---
a.send("SALIR"); time.sleep(0.4); a.s.close()
a2=C(); a2.send("LOGIN|ana|clave123"); check(a2.expect("OK")=="OK|Ana","re-login de ana")
hist=a2.drain(1.0)
check(any(l.endswith("|Ana|hola a todos | con barra") and l.startswith("M|G|general") for l in hist),"historial general")
check(any(l.startswith("M|D|beto|") and "secreto de ana" in l for l in hist),"historial de directos")
check(any(l.startswith("GRUPO|") for l in hist) and any(l.startswith(f"M|R|{gid}|") and "hola grupo" in l for l in hist),"grupo + historial")
check(not any("para borrar" in l for l in hist),"los mensajes borrados NO vuelven en el historial")
check(any(l.startswith("CANAL|") and l.endswith("|Avisos|Ana|1") for l in hist) and sum(l.startswith(f"M|C|{cid}|") for l in hist)==2,"canal + historial")

# --- base de datos ---
import re
con=sqlite3.connect(DB)
n,sal,hs=con.execute("select nombre,sal,hashClave from Usuarios where nombre='Ana'").fetchone()
check("clave123" not in hs and len(hs)==64 and len(sal)==32,"contraseña guardada como hash con sal (no en claro)")
check(con.execute("select count(*) from Usuarios").fetchone()[0]==4,"4 usuarios en BD (tablas intactas tras la inyección)")
check(con.execute("select count(*) from Mensajes").fetchone()[0]>=5,"mensajes persistidos")
check(con.execute("select count(*) from Mensajes where texto like '%para borrar%'").fetchone()[0]==0,"los mensajes borrados ya no están en la BD")
f=[r[0] for r in con.execute("select fechaEnvio from Mensajes")]+[r[0] for r in con.execute("select fechaRegistro from Usuarios")]+[r[0] for r in con.execute("select fechaCreacion from Grupos")]
check(f and all(re.fullmatch(r"\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}",x) for x in f),"las fechas son texto legible AAAA-MM-DD HH:MM:SS (sin convertir): "+f[0])
check(con.execute("select avatar from Usuarios where nombre='beto'").fetchone()[0]=="gato" and con.execute("select avatar from Usuarios where nombre='Ana'").fetchone()[0]=="foto","columna avatar guarda gato / foto")
con.execute("PRAGMA foreign_keys=ON")
check(con.execute("PRAGMA foreign_key_check").fetchall()==[],"integridad referencial correcta")
try:
    con.execute("insert into Mensajes(tipo,idAutor,idGrupo,texto) values('G',1,1,'x')"); check(False,"CHECK debía rechazar mensaje general con idGrupo")
except sqlite3.IntegrityError: check(True,"la BD rechaza mensajes con llaves inconsistentes (CHECK)")
try:
    con.execute("insert into Mensajes(tipo,idAutor,texto) values('G',999,'x')"); check(False,"FK debía rechazar autor inexistente")
except sqlite3.IntegrityError: check(True,"la BD rechaza autores inexistentes (llave foránea)")

a2.s.close(); b.s.close(); d.s.close()
cleanup()

# --- una BD con el esquema de la version anterior se rechaza con un mensaje claro ---
viejo=os.path.join(tempfile.mkdtemp(),'viejo.db'); k=sqlite3.connect(viejo); k.execute("create table usuarios(nombre text)"); k.commit(); k.close()
r=subprocess.run([sys.argv[1], str(PORT+1), viejo], capture_output=True, text=True, timeout=10)
check(r.returncode!=0 and "esquema" in (r.stdout+r.stderr),"un chat.db del esquema anterior se rechaza con un mensaje claro"); print(f"\n{ok} comprobaciones OK")
