# Alternativa a CMake SOLO PARA EL SERVIDOR (ocupa g++, make y unixodbc-dev).
# El cliente se compila con CMake (ver README).

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
LIBS     ?= -lodbc -pthread

servidor: src/servidor/*.cpp src/servidor/*.h src/comun/*.cpp src/comun/*.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) src/servidor/mainServidor.cpp src/servidor/Servidor.cpp src/servidor/BaseDatos.cpp \
		src/comun/Base64.cpp -o servidor $(LIBS)

clean:
	rm -f servidor

.PHONY: clean
