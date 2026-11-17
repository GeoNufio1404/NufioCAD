#pragma once

// Vigilante de paradas de la interfaz. Un hilo aparte mira si la interfaz sigue dando señales de vida; si se queda
// parada más de un umbral, copia su pila de llamadas (Windows) y, cuando vuelve, anota en un archivo cuánto duró y
// dónde estaba. Sirve para encontrar lo que deja la ventana en «No responde» usando la aplicación de verdad.
//
// Se activa con la variable de entorno NUF_VIGILANTE: «1» usa un umbral de 200 ms; otro número, ese número de ms.
// El informe (paradas.txt) queda junto a los de cierres inesperados: %APPDATA%/NufioCAD/NufioCAD/cierres.

#include <QString>

#include <atomic>
#include <thread>

namespace nuf::app {

// --- Captura de la pila de otro hilo (la comparten este vigilante y la prueba de la interfaz) ---------------------

// Un identificador del hilo que llama (hay que pedirlo desde el hilo de la interfaz). nullptr fuera de Windows.
void* currentThreadHandle();
void releaseThreadHandle(void* handle);
// Detiene un instante ese hilo, copia su pila y la descifra: «· función» por línea, con un máximo de líneas.
// Vacío si no se puede (o fuera de Windows).
QString sampleThreadStack(void* handle, int maxLines = 12);

class StallWatch {
public:
    // «thresholdMs»: parada mínima que se anota. Debe crearse en el hilo de la interfaz, con QApplication creada.
    StallWatch(int thresholdMs, const QString& directory);
    ~StallWatch();
    StallWatch(const StallWatch&) = delete;
    StallWatch& operator=(const StallWatch&) = delete;

    // El umbral pedido en NUF_VIGILANTE, o 0 si no está activado.
    static int thresholdFromEnvironment();
    // Carpeta del informe (la de los cierres inesperados).
    static QString defaultDirectory();

    int stalls() const { return stalls_.load(); }
    QString reportPath() const { return reportPath_; }

private:
    void run();
    void report(qint64 durationMs, const QString& stack);

    int thresholdMs_;
    QString reportPath_;
    void* mainThread_ = nullptr;
    std::atomic<qint64> heartbeat_{0};
    std::atomic<bool> running_{false};
    std::atomic<int> stalls_{0};
    std::thread thread_;
};

} // namespace nuf::app
