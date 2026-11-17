#include "Protocolo.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

// Sin MSG_NOSIGNAL, escribir en un socket que el otro lado ya cerro manda
// SIGPIPE y el sistema MATA el programa. Con la bandera, send() solo falla.
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

using namespace std;

bool enviarMensaje(int socket, const string& mensaje) {
	string aEnviar = mensaje + "\n";
	size_t total = 0;

	while (total < aEnviar.size()) {
		ssize_t enviado = send(socket, aEnviar.c_str() + total, aEnviar.size() - total, MSG_NOSIGNAL);

		if (enviado <= 0) {
			return false;
		}

		total += static_cast<size_t>(enviado);
	}

	return true;
}

bool recibirMensaje(int socket, string& mensajeRecibido) {
	mensajeRecibido.clear();
	char byte;

	while (true) {
		ssize_t leido = recv(socket, &byte, 1, 0);

		if (leido <= 0) {
			return false; // cerrado, error o timeout
		}

		if (byte == '\n') {
			if (!mensajeRecibido.empty() && mensajeRecibido.back() == '\r') {
				mensajeRecibido.pop_back();
			}
			return true;
		}

		mensajeRecibido += byte;

		if (mensajeRecibido.size() > 8192) {
			return false; // linea absurdamente larga: se corta la conexion
		}
	}
}

vector<string> separarMaximo(const string& mensaje, size_t maxCampos, char separador) {
	vector<string> campos;
	size_t inicio = 0;

	while (campos.size() + 1 < maxCampos) {
		size_t pos = mensaje.find(separador, inicio);

		if (pos == string::npos) {
			break;
		}

		campos.push_back(mensaje.substr(inicio, pos - inicio));
		inicio = pos + 1;
	}

	campos.push_back(mensaje.substr(inicio));
	return campos;
}

int conectarConTimeout(const string& ip, int puerto, int milisegundos, string& error) {
	addrinfo pistas;
	memset(&pistas, 0, sizeof(pistas));
	pistas.ai_family = AF_INET;
	pistas.ai_socktype = SOCK_STREAM;

	addrinfo* resultado = nullptr;
	int codigo = getaddrinfo(ip.c_str(), to_string(puerto).c_str(), &pistas, &resultado);

	if (codigo != 0 || resultado == nullptr) {
		error = string("No se pudo resolver la direccion: ") + gai_strerror(codigo);
		return -1;
	}

	int fd = socket(AF_INET, SOCK_STREAM, 0);

	if (fd == -1) {
		freeaddrinfo(resultado);
		error = "No se pudo crear el socket.";
		return -1;
	}

	// connect() sin bloquear + poll(): asi podemos poner un limite de tiempo.
	int banderas = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, banderas | O_NONBLOCK);

	int r = connect(fd, resultado->ai_addr, resultado->ai_addrlen);
	freeaddrinfo(resultado);

	if (r == -1 && errno != EINPROGRESS) {
		error = string("No se pudo conectar: ") + strerror(errno);
		close(fd);
		return -1;
	}

	if (r == -1) {
		pollfd p;
		p.fd = fd;
		p.events = POLLOUT;
		p.revents = 0;

		int listo = poll(&p, 1, milisegundos);

		if (listo <= 0) {
			error = "El servidor no respondio a tiempo (revisa la IP y el puerto).";
			close(fd);
			return -1;
		}

		int err = 0;
		socklen_t tam = sizeof(err);
		getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &tam);

		if (err != 0) {
			error = string("No se pudo conectar: ") + strerror(err);
			close(fd);
			return -1;
		}
	}

	fcntl(fd, F_SETFL, banderas); // de vuelta a modo bloqueante
	return fd;
}

void ponerTimeoutRecepcion(int socket, int milisegundos) {
	timeval tv;
	tv.tv_sec = milisegundos / 1000;
	tv.tv_usec = (milisegundos % 1000) * 1000;
	setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

