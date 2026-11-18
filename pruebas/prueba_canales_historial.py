"""Pruebas de lista privada de canal, historial ilimitado y avisos persistentes.
Uso: python3 pruebas/prueba_canales_historial.py ./servidor [puerto]
Siempre usa una base de datos temporal.
"""
import os
import select
import socket
import sqlite3
import subprocess
import sys
import tempfile
import time

server = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else './servidor')
port = int(sys.argv[2]) if len(sys.argv) > 2 else 5115
ruta = os.path.join(tempfile.mkdtemp(prefix='telechat-hist-'), 'test.db')
p = subprocess.Popen([server, str(port), ruta], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)

class Cliente:
    def __init__(self):
        self.s = socket.create_connection(('127.0.0.1', port), timeout=3)
        self.s.setblocking(False)
        self.buf = b''
        self.queue = []
    def send(self, linea):
        self.s.sendall((linea + '\n').encode())
    def receive(self, timeout=0.2):
        if self.queue:
            return self.queue.pop(0)
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            while b'\n' in self.buf:
                l, self.buf = self.buf.split(b'\n', 1)
                self.queue.append(l.decode())
            if self.queue:
                return self.queue.pop(0)
            if not select.select([self.s], [], [], max(0.0, end-time.monotonic()))[0]:
                break
            b = self.s.recv(65536)
            if not b: return None
            self.buf += b
        return None
    def collect_until(self, condition, timeout=12):
        result = []
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            l = self.receive(0.2)
            if l is None: continue
            result.append(l)
            if condition(result): break
        return result
    def until(self, prefix, timeout=5):
        lines = self.collect_until(lambda ls: ls[-1].startswith(prefix), timeout)
        assert lines and lines[-1].startswith(prefix), (prefix, lines[-10:])
        return lines[-1]
    def flush(self):
        return self.collect_until(lambda _: False, 0.15)
    def close(self):
        self.s.close()

checks = 0
def check(c, desc):
    global checks
    assert c, desc
    checks += 1
    print('ok  ', desc)

try:
    time.sleep(0.25)
    a, b, c = Cliente(), Cliente(), Cliente()
    for cl, name in ((a,'Ana'),(b,'Beto'),(c,'Carla')):
        cl.send('REGISTRO|' + name + '|clave123|')
        check(cl.until('OK|') == 'OK|' + name, 'registra ' + name)
    for cl in (a,b,c): cl.flush()

    a.send('CANAL_NUEVO|Noticias')
    cid = a.until('CANAL|').split('|')[1]
    b.until('CANAL|'); c.until('CANAL|')
    b.send('CANAL_UNIR|' + cid)
    check(b.until('CANAL|').endswith('|1'), 'Beto entra en canal')

    b.send('CANAL_MIEMBROS_PEDIR|' + cid)
    check('Solo el creador' in b.until('AVISO|'), 'suscriptor NO puede obtener nombres')
    c.send('CANAL_MIEMBROS_PEDIR|' + cid)
    check('Solo el creador' in c.until('AVISO|'), 'ajeno NO puede obtener nombres')
    a.send('CANAL_MIEMBROS_PEDIR|' + cid)
    resp = a.collect_until(lambda r: r[-1].startswith('CANAL_MIEMBROS_FIN|'))
    check(resp[0] == f'CANAL_MIEMBROS_INICIO|{cid}', 'lista comienza para el dueño')
    check(set(x.split('|')[2] for x in resp if x.startswith('CANAL_MIEMBRO|')) == {'Ana','Beto'},
          'lista incluye dueño y suscriptor')
    check(resp[-1] == f'CANAL_MIEMBROS_FIN|{cid}', 'lista termina correctamente')

    # >600 mensajes: prueba el limite del servidor; el cliente debe ademas mantenerlos sin podar.
    numero = 625
    for i in range(numero):
        a.send(f'CMSG|{cid}|aviso_{i:04d}')
    def collect_msgs(cli, n):
        ls = cli.collect_until(lambda r: sum(x.startswith(f'M|C|{cid}|') for x in r) >= n, 20)
        return [x for x in ls if x.startswith(f'M|C|{cid}|')]
    messages = collect_msgs(a,numero)
    check(len(messages) == numero, 'el propietario recibe 625 mensajes')
    check(len(collect_msgs(b, numero)) == numero, 'el miembro recibe 625 mensajes')
    middle_id = messages[123].split('|')[3]
    b.send('BORRAR|' + middle_id)
    check('Solo puedes' in b.until('AVISO|'), 'usuario ajeno no borra mensaje')
    a.send('BORRAR|' + middle_id)
    check(a.until('DEL|') == f'DEL|C|{cid}|{middle_id}', 'dueño recibe reemplazo en vivo')
    check(b.until('DEL|') == f'DEL|C|{cid}|{middle_id}', 'miembro recibe reemplazo en vivo')

    # Carla entra por primera vez despues del borrado; debe recibir historial COMPLETO
    # y una marca MD, no el texto original.
    c.send('CANAL_UNIR|' + cid)
    check(c.until('CANAL|').endswith('|1'), 'Carla entra al canal')
    full = c.collect_until(lambda r: sum(x.startswith((f'M|C|{cid}|', f'MD|C|{cid}|')) for x in r) >= numero, 20)
    entries = [x for x in full if x.startswith((f'M|C|{cid}|', f'MD|C|{cid}|'))]
    check(len(entries) == numero, 'al entrar recibe 625 posiciones de historial (sin limite 50)')
    check(entries[0].endswith('aviso_0000') and entries[-1].endswith('aviso_0624'), 'historial desde primero hasta ultimo')
    check(len([x for x in entries if x.startswith('MD|')]) == 1, 'historial incluye marca de mensaje eliminado')
    check(any(x.startswith(f'MD|C|{cid}|{middle_id}|') and x.endswith('|Ana') for x in entries),
          'marca conserva autor sin texto')
    check(not any('aviso_0123' in x for x in entries), 'el contenido eliminado no vuelve')

    a.send('CANAL_MIEMBROS_PEDIR|' + cid)
    resp=a.collect_until(lambda r:r[-1].startswith('CANAL_MIEMBROS_FIN|'))
    check(set(x.split('|')[2] for x in resp if x.startswith('CANAL_MIEMBRO|')) == {'Ana','Beto','Carla'},
          'el propietario ve nuevos miembros al actualizar')

    # Un mensaje directo sin mensajes restantes igualmente recupera el aviso tras iniciar sesion.
    a.send('DM|Beto|texto_privado_eliminado')
    dm = a.until('M|D|')
    b.until('M|D|')
    dm_id=dm.split('|')[3]
    a.send('BORRAR|' + dm_id)
    a.until('DEL|D|'); b.until('DEL|D|')
    b.send('SALIR')
    b.close()
    time.sleep(0.15)
    b2=Cliente(); b2.send('LOGIN|Beto|clave123')
    b2.until('OK|')
    initial = b2.collect_until(lambda r: any(x.startswith(f'MD|D|Ana|{dm_id}|') for x in r), 6)
    check(any(x.startswith(f'MD|D|Ana|{dm_id}|') for x in initial),
          'marcador de directo persiste aunque ya no hay mensajes activos')

    con=sqlite3.connect(ruta)
    check(con.execute('SELECT count(*) FROM Mensajes WHERE idMensaje=?', (int(middle_id),)).fetchone()[0] == 0,
          'texto borrado no existe en Mensajes')
    check(con.execute('SELECT count(*) FROM MensajesEliminados WHERE idMensaje=?', (int(middle_id),)).fetchone()[0] == 1,
          'SQLite mantiene solo constancia del borrado')
    con.close()

    # Los borrados en cascada siguen funcionando también sobre las constancias nuevas.
    a.send('CUENTA_ELIMINAR|clave123')
    check(a.until('CUENTA_ELIMINADA') == 'CUENTA_ELIMINADA', 'cuenta de Ana eliminada')
    check(c.until('CANAL_ELIMINADO|') == f'CANAL_ELIMINADO|{cid}', 'canal avisa a Carla de su eliminación')
    check(b2.until('CANAL_ELIMINADO|') == f'CANAL_ELIMINADO|{cid}', 'canal avisa a Beto de su eliminación')
    con=sqlite3.connect(ruta)
    check(con.execute('SELECT count(*) FROM MensajesEliminados WHERE idMensaje IN (?,?)',
                      (int(middle_id),int(dm_id))).fetchone()[0] == 0,
          'borrado de cuenta limpia constancias de canal y directo por CASCADE')
    con.close()
    for cl in (a,c,b2):cl.close()
    print(f'\n{checks} comprobaciones nuevas OK')
finally:
    p.terminate()
    try: p.wait(timeout=3)
    except subprocess.TimeoutExpired: p.kill()
