// =====================================================================
//  CHAT - Cliente de CONSOLA (no necesita SFML ni pantalla grafica)
//
//  Sirve para probar el servidor y como plan B si por SSH no se puede
//  abrir la ventana de SFML. Habla el MISMO protocolo que el cliente
//  grafico, asi que ambos pueden chatear entre si.
//
//  Uso:  ./cliente_consola <usuario> [ip] [puerto]
//  Comandos:  /w usuario texto   mensaje privado     /salir   salir
// =====================================================================

#include "Protocolo.h"
#include "Config.h"

#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <string>
#include <vector>

using namespace std;

static void mostrar(const string& linea) {
	vector<string> c = separarMaximo(linea, 4);
	const string& cmd = c[0];

	if (cmd == "MSG" && c.size() >= 4) {
		cout << "[" << c[1] << "] " << c[2] << ": " << c[3] << endl;
	} else if (cmd == "PRIV" && c.size() >= 4) {
		cout << "[" << c[1] << "] (privado de " << c[2] << "): " << c[3] << endl;
	} else if (cmd == "PRIVOUT" && c.size() >= 4) {
		cout << "[" << c[1] << "] (privado para " << c[2] << "): " << c[3] << endl;
	} else if (cmd == "SYS" && c.size() >= 2) {
		cout << "*** " << c[1] << endl;
	} else if (cmd == "USERS" && c.size() >= 2) {
		cout << "--- En linea: " << c[1] << endl;
	}
}

int main(int argc, char* argv[]) {
	if (argc < 2) {
		cout << "Uso: " << argv[0] << " <usuario> [ip] [puerto]" << endl;
		return 1;
	}

	string usuario = argv[1];
	string ip = (argc > 2) ? argv[2] : IP_SERVIDOR_POR_DEFECTO;
	int puerto = (argc > 3) ? atoi(argv[3]) : PUERTO_POR_DEFECTO;

	string error;
	int fd = conectarConTimeout(ip, puerto, 3000, error);

	if (fd == -1) {
		cout << error << endl;
		return 1;
	}

	enviarMensaje(fd, "NICK|" + usuario);
	ponerTimeoutRecepcion(fd, 3000);

	string respuesta;

	if (!recibirMensaje(fd, respuesta) || respuesta.rfind("OK", 0) != 0) {
		cout << "No se pudo entrar: " << (respuesta.empty() ? "sin respuesta del servidor" : respuesta.substr(4)) << endl;
		close(fd);
		return 1;
	}

	ponerTimeoutRecepcion(fd, 0);
	cout << respuesta.substr(3) << "  (/w usuario texto = privado, /salir = salir)" << endl;

	// poll() sobre el teclado (0) y el socket a la vez.
	pollfd fds[2];
	fds[0].fd = 0;  fds[0].events = POLLIN;
	fds[1].fd = fd; fds[1].events = POLLIN;

	bool seguir = true;

	while (seguir) {
		fds[0].revents = fds[1].revents = 0;

		if (poll(fds, 2, -1) < 0) {
			break;
		}

		if (fds[1].revents & (POLLIN | POLLHUP)) {
			string linea;

			if (!recibirMensaje(fd, linea)) {
				cout << "*** Conexion cerrada por el servidor." << endl;
				break;
			}

			mostrar(linea);
		}

		if (fds[0].revents & POLLIN) {
			string texto;

			if (!getline(cin, texto) || texto == "/salir") {
				enviarMensaje(fd, "SALIR");
				seguir = false;
			} else if (texto.rfind("/w ", 0) == 0) {
				size_t esp = texto.find(' ', 3);

				if (esp == string::npos) {
					cout << "Uso: /w usuario texto" << endl;
				} else {
					enviarMensaje(fd, "PRIV|" + texto.substr(3, esp - 3) + "|" + texto.substr(esp + 1));
				}
			} else if (!texto.empty()) {
				enviarMensaje(fd, "MSG|" + texto);
			}
		}
	}

	close(fd);
	return 0;
}

