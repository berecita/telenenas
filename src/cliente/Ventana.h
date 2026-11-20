#ifndef VENTANA_H
#define VENTANA_H

#include <SFML/Graphics.hpp>
#include <string>
#include <vector>

// ---------------------------------------------------------------------
// Ventana: un envoltorio simple de sf::RenderWindow.
//
// REGLA DE ORO DE SFML (la neta importante): una ventana abierta necesita que le atiendas los
// eventos (pollEvent) todo el tiempo. Si la abres y despues te quedas
// esperando otra cosa (por ejemplo un cin >> ...), el sistema cree que el
// progama se congelo y te sale "Forzar salida". Por eso:
//   - El constructor NO abre la ventana (nomas deja todo listo).
//   - La ventana se abre con abrir() o con loop(), y desde ahi
//     el programa tiene que estar corriendo SU ciclo de eventos.
// ---------------------------------------------------------------------
class Ventana {
private:
    sf::RenderWindow window;
    sf::Font font;
    bool fuenteCargada;

    std::string titulo;
    unsigned int ancho;
    unsigned int alto;

    std::vector<sf::Text> textos; // solo pa la API simple (agregarTexto/loop)

    bool cargarFuente();

public:
    Ventana(const std::string& nombreVentana, int ancho, int alto);

    // ---- ciclo de vida ----
    void abrir();                 // crea la ventana en pantalla (si no estaba abierta)
    void cerrar();
    bool estaAbierta() const;

    // ---- acceso a SFML pa quien arma su propio ciclo (ver mainAdministrador.cpp) ----
    sf::RenderWindow& getWindow();
    bool tieneFuente() const;

    // ---- dibujo ----
    // Dibuja texto (acepta acentos y ñ: se lee como UTF-8).
    // Ancho en pixeles que ocupa el texto (pa ajustar lineas y alinear).
    float medirTexto(const std::string& texto, unsigned int size = 20);

    void dibujarTexto(const std::string& texto, float x, float y,
                      unsigned int size = 20, sf::Color color = sf::Color::Black);

    // Dibuja el texto centrado dentro de un rectangulo (pa los botones).
    void dibujarTextoCentrado(const std::string& texto, const sf::FloatRect& area,
                              unsigned int size = 20, sf::Color color = sf::Color::White);

    // ---- API simple: una ventana que solo muestra textos fijos ----
    void agregarTexto(const std::string& texto, int x, int y, int size = 20);
    void limpiar();

    // Abre la ventana, muestra los textos que agregaste y se queda bloqueado hasta que
    // el usuario la cierra. Cuando regresa, la ventana ya esta cerrada.
    void loop();
};

#endif
