cmake_minimum_required(VERSION 3.16)
project(chat_sfml CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Release)
endif()

# El servidor usa sockets POSIX: solo Linux/macOS. El cliente compila en los tres sistemas.
if(UNIX)
	set(SERVIDOR_POR_DEFECTO ON)
else()
	set(SERVIDOR_POR_DEFECTO OFF)
endif()
option(BUILD_SERVIDOR "Compilar el servidor (solo Linux/macOS)" ${SERVIDOR_POR_DEFECTO})
option(BUILD_CLIENTE "Compilar el cliente grafico" ON)

# ======================= SERVIDOR (sin SFML) =======================
if(BUILD_SERVIDOR)
	find_package(Threads REQUIRED)

	add_executable(servidor
		src/servidor/mainServidor.cpp
		src/servidor/Servidor.cpp
		src/servidor/BaseDatos.cpp
		src/comun/Base64.cpp)
	target_link_libraries(servidor PRIVATE Threads::Threads)

	# SQLite: la del sistema (libsqlite3-dev) o, si no hay permisos para instalarla,
	# el "amalgamation" descargado en third_party/sqlite3/ (ver README).
	if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/sqlite3/sqlite3.c")
		add_library(sqlite3_local STATIC third_party/sqlite3/sqlite3.c)
		target_include_directories(sqlite3_local PUBLIC third_party/sqlite3)
		target_link_libraries(sqlite3_local PUBLIC ${CMAKE_DL_LIBS} Threads::Threads)
		target_link_libraries(servidor PRIVATE sqlite3_local)
	else()
		find_package(SQLite3 REQUIRED)
		target_link_libraries(servidor PRIVATE SQLite::SQLite3)
	endif()
endif()

# ======================= CLIENTE (SFML) =======================
if(BUILD_CLIENTE)
	# Usa SFML 2.5/2.6 instalada; si no hay (o es SFML 3, que es incompatible),
	# la descarga y compila sola con FetchContent (necesita internet la primera vez).
	find_package(SFML 2.5 COMPONENTS network graphics window system QUIET)

	if(NOT SFML_FOUND)
		message(STATUS "SFML 2.x no encontrada en el sistema: se descargara SFML 2.6.1")
		include(FetchContent)
		set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
		set(SFML_BUILD_AUDIO OFF CACHE BOOL "" FORCE)
		set(SFML_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
		set(SFML_BUILD_DOC OFF CACHE BOOL "" FORCE)
		FetchContent_Declare(SFML
			GIT_REPOSITORY https://github.com/SFML/SFML.git
			GIT_TAG 2.6.1
			GIT_SHALLOW ON)
		FetchContent_MakeAvailable(SFML)
	endif()

	add_executable(cliente
		src/cliente/mainCliente.cpp
		src/cliente/Chat.cpp
		src/cliente/Conexion.cpp
		src/cliente/Avatares.cpp
		src/cliente/Ventana.cpp
		src/cliente/Widgets.cpp
		src/comun/Base64.cpp)
	target_link_libraries(cliente PRIVATE sfml-network sfml-graphics sfml-window sfml-system)

	# Para que la fuente se encuentre aunque muevas el ejecutable o lo abras con doble clic.
	target_compile_definitions(cliente PRIVATE RECURSOS_DIR="${CMAKE_SOURCE_DIR}/recursos")
	add_custom_command(TARGET cliente POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${CMAKE_SOURCE_DIR}/recursos" "$<TARGET_FILE_DIR:cliente>/recursos")
endif()
