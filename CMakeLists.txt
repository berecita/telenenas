cmake_minimum_required(VERSION 3.16)
project(chat_sfml)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

include_directories(include)
find_package(Threads REQUIRED)

# ---- Protocolo (sockets) : lo usan servidor y los dos clientes ----
add_library(protocolo STATIC src/Protocolo.cpp)

# ---- Servidor: solo consola, NO necesita SFML ----
add_executable(servidor src/mainServidor.cpp)
target_link_libraries(servidor PRIVATE protocolo Threads::Threads)

# ---- Cliente de consola: plan B / pruebas, NO necesita SFML ----
add_executable(cliente_consola src/mainClienteConsola.cpp)
target_link_libraries(cliente_consola PRIVATE protocolo)

# ---- Cliente grafico: SOLO se compila si hay SFML instalada ----
find_package(SFML 2.5 COMPONENTS graphics window system QUIET)

if(SFML_FOUND)
	add_library(interfaz STATIC src/Ventana.cpp src/Widgets.cpp)
	# Ruta ABSOLUTA de recursos/ para encontrar la fuente desde cualquier carpeta
	target_compile_definitions(interfaz PRIVATE RECURSOS_DIR="${CMAKE_SOURCE_DIR}/recursos")
	target_link_libraries(interfaz PUBLIC sfml-graphics sfml-window sfml-system)

	add_executable(cliente src/mainCliente.cpp)
	target_link_libraries(cliente PRIVATE interfaz protocolo Threads::Threads)
else()
	message(WARNING "SFML no encontrada: solo se compilan servidor y cliente_consola. "
	                "Instala libsfml-dev para compilar el cliente grafico.")
endif()

