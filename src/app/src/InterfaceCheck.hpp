#pragma once

// Prueba automática de la interfaz (NufioCAD --prueba-interfaz): con la ventana real abre cada herramienta y cada
// generador, cambia un valor, acepta, deshace, prueba las unidades, la sección y la medición, y comprueba que no hay
// errores ni paradas largas de la interfaz. Escribe un informe y devuelve 0 si todo salió bien.

#include <QElapsedTimer>
#include <QObject>
#include <QString>

#include <QMetaObject>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class QAction;
class QDoubleSpinBox;
class QPushButton;

namespace nuf::app {

class MainWindow;

class InterfaceCheck {
public:
    // «thresholdMs»: la parada más larga de la interfaz que se da por buena.
    InterfaceCheck(MainWindow& window, QString reportPath, int thresholdMs);

    ~InterfaceCheck();

    int run();
    // --captura-temas: guarda capturas de la ventana y de los menús con el tema claro y el oscuro, para mirarlas.
    int captureThemes(const QString& folder);

private:
    struct Result {
        QString name;
        bool ok = true;
        QString detail;
        qint64 blockedMs = 0;  // lo más que se paró la interfaz en este paso
        QString blockedBy;     // y qué la paró
    };

    // Ejecuta un paso; las excepciones y los fallos de «expect» lo marcan como fallido.
    void step(const QString& name, const std::function<void()>& body);
    void expect(bool condition, const QString& what);
    // Una llamada que corre en el hilo de la interfaz: se mide cuánto la bloquea.
    void blocking(const QString& what, const std::function<void()>& call);
    // Deja trabajar a la interfaz (y a los hilos que avisan) durante «ms».
    void settle(int ms);
    bool waitFor(const std::function<bool()>& condition, int timeoutMs);
    // Espera a que ni el visor ni la vista previa tengan trabajo pendiente.
    void waitIdle();

    QDoubleSpinBox* spin(int index) const;
    QPushButton* button(const QString& text) const;
    // Abre un comando, mueve un valor y lo acepta; devuelve cuántas operaciones añadió.
    int openChangeAccept(QAction* action, int spinIndex, const QString& choice = {});
    void undoAll();

    MainWindow& window_;
    QString reportPath_;
    int thresholdMs_;
    // Un vigilante en otro hilo: si la interfaz deja de dar señales de vida, anota dónde está parada.
    void startWatchdog();
    void stopWatchdog();
    qint64 nowMs() const;

    QMetaObject::Connection busyConnection_;
    std::atomic<qint64> heartbeat_{0};
    std::atomic<int> currentStep_{-1};
    std::atomic<bool> watching_{false};
    std::thread watchdog_;
    std::mutex samplesMutex_;
    std::vector<std::pair<int, QString>> samples_;
    void* mainThread_ = nullptr;
    bool viewBusy_ = false;
    QString lastAction_;  // la última llamada que se hizo (para saber qué provocó una parada)
    qint64 stallMs_ = 0;  // la mayor parada vista por el vigilante durante las esperas
    std::vector<Result> results_;
    Result* current_ = nullptr;
};

} // namespace nuf::app
