// =====================================================================
//  CHAT - Servidor (solo consola, NO necesita SFML)
//
//  Misma idea que Servidor.cpp de upifood: un socket que escucha, y un
//  HILO por cada cliente conectado. La diferencia es que aqui el servidor
//  reenvia (difunde) cada mensaje a TODOS los clientes conectados.
//
//  Uso:   ./servidor [puerto]        (por defecto 5000)
//  Parar: Ctrl+C (cierra todo ordenadamente)
// =====================================================================

#include "Protocolo.h"
#include "Config.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <csignal>
#include <cstring>
#include <ctime>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace std;

static const size_t MAX_NICK = 16;
static const size_t MAX_TEXTO = 500; // bytes por mensaje

// socket -> nombre de usuario. Lo tocan TODOS los hilos, por eso el mutex.
static map<int, string> clientes;
static mutex mutexClientes;

static mutex mutexLog;
static atomic<bool> apagando{false};

extern "C" void manejarSenal(int) {
	apagando.store(true); // el manejador solo levanta una bandera (async-signal-safe)
}

static void escribirLog(const string& texto) {
	lock_guard<mutex> g(mutexLog);
	cout << texto << endl;
}

static string horaActual() {
	time_t ahora = time(nullptr);
	tm local;
	localtime_r(&ahora, &local);
	char buffer[8];
	strftime(buffer, sizeof(buffer), "%H:%M", &local);
	return buffer;
}

// ---- limpieza de texto ----

static string recortarEspacios(const string& s) {
	size_t i = 0, j = s.size();
	while (i < j && isspace(static_cast<unsigned char>(s[i]))) i++;
	while (j > i && isspace(static_cast<unsigned char>(s[j - 1]))) j--;
	return s.substr(i, j - i);
}

// Corta a `maximo` bytes sin partir un caracter UTF-8 a la mitad.
static string limitarBytes(const string& s, size_t maximo) {
	if (s.size() <= maximo) return s;

	size_t corte = maximo;
	while (corte > 0 && (static_cast<unsigned char>(s[corte]) & 0xC0) == 0x80) {
		corte--;
	}
	return s.substr(0, corte);
}

static string minusculas(string s) {
	transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return tolower(c); });
	return s;
}

// Devuelve "" si el nombre sirve, o el motivo del rechazo.
// (El llamador debe tener tomado mutexClientes para revisar repetidos.)
static string validarNombreBloqueado(const string& nombre) {
	if (nombre.empty()) return "El nombre no puede estar vacio.";
	if (nombre.size() > MAX_NICK) return "El nombre admite maximo 16 caracteres.";

	for (unsigned char c : nombre) {
		if (c < 32 || c == '|' || c == ',' || c == 127) {
			return "El nombre no puede llevar '|' ni ','.";
		}
	}

	string buscado = minusculas(nombre);
	for (const auto& par : clientes) {
		if (minusculas(par.second) == buscado) {
			return "Ese nombre ya esta en uso, elige otro.";
		}
	}

	return "";
}

// ---- envio a clientes ----

// Manda a todos los que ya entraron al chat.
static void difundir(const string& mensaje) {
	lock_guard<mutex> g(mutexClientes);

	for (const auto& par : clientes) {
		enviarMensaje(par.first, mensaje);
	}
}

static void difundirUsuarios() {
	string lista;

	{
		lock_guard<mutex> g(mutexClientes);
		for (const auto& par : clientes) {
			if (!lista.empty()) lista += ",";
			lista += par.second;
		}
	}

	difundir("USERS|" + lista);
}

// ---- atencion de un cliente (corre en su propio hilo) ----

static void atenderCliente(int fd) {
	string linea;
	string nick;

	// 1) Registro: el primer mensaje valido debe ser NICK|nombre.
	while (!apagando.load() && recibirMensaje(fd, linea)) {
		vector<string> campos = separarMaximo(linea, 2);

		if (campos[0] != "NICK" || campos.size() < 2) {
			enviarMensaje(fd, "ERR|Primero debes mandar tu nombre.");
			continue;
		}

		string candidato = recortarEspacios(campos[1]);
		string motivo;

		{
			lock_guard<mutex> g(mutexClientes);
			motivo = validarNombreBloqueado(candidato);

			if (motivo.empty()) {
				clientes[fd] = candidato; // desde aqui ya recibe difusiones
				nick = candidato;
			}
		}

		if (!motivo.empty()) {
			enviarMensaje(fd, "ERR|" + motivo);
			continue;
		}

		enviarMensaje(fd, "OK|Bienvenido al chat, " + nick + ".");
		break;
	}

	if (nick.empty()) { // se desconecto antes de registrarse
		close(fd);
		return;
	}

	escribirLog("[+] " + nick + " entro al chat.");
	difundir("SYS|" + nick + " se unio al chat.");
	difundirUsuarios();

	// 2) Ciclo principal: leer lo que manda y reenviarlo.
	while (recibirMensaje(fd, linea)) {
		// MSG lleva 2 campos (el texto es TODO lo que sigue, aunque traiga '|');
		// PRIV lleva 3 (destino y texto).
		vector<string> campos = separarMaximo(linea, 2);
		const string comando = campos[0];

		if (comando == "PRIV") {
			campos = separarMaximo(linea, 3);
		}

		if (comando == "SALIR") {
			break;
		}

		if (comando == "MSG" && campos.size() >= 2) {
			string texto = limitarBytes(recortarEspacios(campos[1]), MAX_TEXTO);
			if (texto.empty()) continue;

			escribirLog("[" + horaActual() + "] " + nick + ": " + texto);
			difundir("MSG|" + horaActual() + "|" + nick + "|" + texto);

		} else if (comando == "PRIV" && campos.size() >= 3) {
			string destino = recortarEspacios(campos[1]);
			string texto = limitarBytes(recortarEspacios(campos[2]), MAX_TEXTO);
			if (texto.empty()) continue;

			int fdDestino = -1;
			string nombreReal;

			{
				lock_guard<mutex> g(mutexClientes);
				for (const auto& par : clientes) {
					if (minusculas(par.second) == minusculas(destino)) {
						fdDestino = par.first;
						nombreReal = par.second;
					}
				}
			}

			if (fdDestino == -1) {
				enviarMensaje(fd, "SYS|El usuario '" + destino + "' no esta conectado.");
			} else {
				string hora = horaActual();
				escribirLog("[" + hora + "] (privado) " + nick + " -> " + nombreReal + ": " + texto);
				enviarMensaje(fdDestino, "PRIV|" + hora + "|" + nick + "|" + texto);
				enviarMensaje(fd, "PRIVOUT|" + hora + "|" + nombreReal + "|" + texto);
			}
		}
		// cualquier otra cosa se ignora
	}

	// 3) Limpieza: sacarlo de la lista ANTES de cerrar el socket.
	{
		lock_guard<mutex> g(mutexClientes);
		clientes.erase(fd);
	}

	close(fd);
	escribirLog("[-] " + nick + " salio del chat.");

	if (!apagando.load()) {
		difundir("SYS|" + nick + " salio del chat.");
		difundirUsuarios();
	}
}

int main(int argc, char* argv[]) {
	int puerto = PUERTO_POR_DEFECTO;

	if (argc > 1) {
		int p = atoi(argv[1]);
		if (p <= 0 || p > 65535) {
			cout << "Uso: " << argv[0] << " [puerto]" << endl;
			return 1;
		}
		puerto = p;
	}

	struct sigaction accion;
	memset(&accion, 0, sizeof(accion));
	accion.sa_handler = manejarSenal;
	sigemptyset(&accion.sa_mask);
	sigaction(SIGINT, &accion, nullptr);
	sigaction(SIGTERM, &accion, nullptr);

	int servidor = socket(AF_INET, SOCK_STREAM, 0);

	if (servidor == -1) {
		cout << "Error creando el socket del servidor." << endl;
		return 1;
	}

	int opt = 1;
	setsockopt(servidor, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); // reusar el puerto al reiniciar

	sockaddr_in direccion;
	memset(&direccion, 0, sizeof(direccion));
	direccion.sin_family = AF_INET;
	direccion.sin_addr.s_addr = INADDR_ANY;
	direccion.sin_port = htons(static_cast<uint16_t>(puerto));

	if (bind(servidor, reinterpret_cast<sockaddr*>(&direccion), sizeof(direccion)) == -1) {
		cout << "Error en bind() del puerto " << puerto
		     << ". Puede que otro programa (u otro alumno) ya lo use; prueba con otro puerto." << endl;
		close(servidor);
		return 1;
	}

	if (listen(servidor, 10) == -1) {
		cout << "Error en listen()." << endl;
		close(servidor);
		return 1;
	}

	cout << "Servidor de chat escuchando en el puerto " << puerto << " (Ctrl+C para detener)..." << endl;

	// poll() con tiempo limite: asi revisamos la bandera de apagado cada medio segundo
	// (un accept() "a secas" no se enteraria del Ctrl+C).
	while (!apagando.load()) {
		pollfd p;
		p.fd = servidor;
		p.events = POLLIN;
		p.revents = 0;

		int listo = poll(&p, 1, 500);

		if (listo <= 0) {
			continue; // timeout o interrumpido por la senal
		}

		sockaddr_in direccionCliente;
		socklen_t tam = sizeof(direccionCliente);
		int fd = accept(servidor, reinterpret_cast<sockaddr*>(&direccionCliente), &tam);

		if (fd == -1) {
			continue;
		}

		char ip[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &direccionCliente.sin_addr, ip, sizeof(ip));
		escribirLog(string("Nueva conexion desde ") + ip);

		thread(atenderCliente, fd).detach(); // un hilo por cliente
	}

	cout << "\nApagando servidor..." << endl;

	// Cerrar el lado de lectura de cada cliente hace que su recv() regrese
	// y su hilo termine solo.
	{
		lock_guard<mutex> g(mutexClientes);
		for (const auto& par : clientes) {
			enviarMensaje(par.first, "SYS|El servidor se esta apagando.");
			shutdown(par.first, SHUT_RDWR);
		}
	}

	// Esperar (maximo 2 s) a que los hilos terminen de limpiarse.
	for (int i = 0; i < 20; i++) {
		{
			lock_guard<mutex> g(mutexClientes);
			if (clientes.empty()) break;
		}
		this_thread::sleep_for(chrono::milliseconds(100));
	}

	close(servidor);
	cout << "Servidor detenido." << endl;
	return 0;
}

