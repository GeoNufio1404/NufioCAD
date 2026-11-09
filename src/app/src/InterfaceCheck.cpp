#include "InterfaceCheck.hpp"

#include "FeatureTools.hpp"
#include "MainWindow.hpp"
#include "Units.hpp"

#include <nuf/commands/FeatureCommands.hpp>
#include <nuf/render/Viewport.hpp>

#include <QAction>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QLabel>
#include <QDockWidget>
#include <QRegularExpression>
#include <QColor>
#include <optional>
#include <QDoubleSpinBox>
#include <QImage>
#include <QElapsedTimer>
#include <QFile>
#include <QPushButton>
#include <QTextStream>
#include <QEvent>
#include <QThread>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#endif

// La copia de la pila de la hebra parada, para recorrerla sin tocarla.
#ifdef _WIN32
namespace {
DWORD64 gSnapshotBase = 0;
SIZE_T gSnapshotSize = 0;
unsigned char gSnapshotData[256 * 1024];

BOOL WINAPI readSnapshot(HANDLE process, DWORD64 address, PVOID buffer, DWORD size, PDWORD done)
{
    if (address >= gSnapshotBase && address + size <= gSnapshotBase + gSnapshotSize) {
        std::memcpy(buffer, gSnapshotData + (address - gSnapshotBase), size);
        *done = size;
        return TRUE;
    }
    SIZE_T count = 0;
    const BOOL ok = ReadProcessMemory(process, reinterpret_cast<LPCVOID>(address), buffer, size, &count);
    *done = DWORD(count);
    return ok;
}
} // namespace
#endif

namespace nuf::app {

InterfaceCheck::InterfaceCheck(MainWindow& window, QString reportPath, int thresholdMs)
    : window_(window)
    , reportPath_(std::move(reportPath))
    , thresholdMs_(thresholdMs)
{
    busyConnection_ = QObject::connect(window_.viewport_, &render::Viewport::busyChanged, window_.viewport_,
                                       [this](bool busy) { viewBusy_ = busy; });
    startWatchdog();
}

InterfaceCheck::~InterfaceCheck()
{
    stopWatchdog();
    QObject::disconnect(busyConnection_);
}

qint64 InterfaceCheck::nowMs() const
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void InterfaceCheck::startWatchdog()
{
#ifdef _WIN32
    HANDLE self = nullptr;
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &self, 0, FALSE, DUPLICATE_SAME_ACCESS);
    mainThread_ = self;
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(GetCurrentProcess(), nullptr, TRUE);
#endif
    heartbeat_ = nowMs();
    watching_ = true;
    watchdog_ = std::thread([this] {
        bool sampled = false;
        while (watching_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            if (nowMs() - heartbeat_ < 1000) {
                sampled = false;
                continue;
            }
            if (sampled) {
                continue;
            }
            sampled = true;
#ifdef _WIN32
            // La interfaz lleva más de un segundo sin dar señales. Se la detiene un instante, solo para copiar sus
            // registros y su pila (sin tocar el montón ni cargar símbolos: la hebra parada podría tenerlos
            // bloqueados), y se la deja seguir; la pila se descifra después sobre la copia.
            HANDLE thread = static_cast<HANDLE>(mainThread_);
            CONTEXT context{};
            context.ContextFlags = CONTEXT_FULL;
            DWORD64 stackBase = 0;
            SIZE_T stackSize = 0;
            bool captured = false;
            if (SuspendThread(thread) != DWORD(-1)) {
                if (GetThreadContext(thread, &context)) {
                    MEMORY_BASIC_INFORMATION region{};
                    if (VirtualQuery(reinterpret_cast<void*>(context.Rsp), &region, sizeof region)) {
                        const DWORD64 end = reinterpret_cast<DWORD64>(region.BaseAddress) + region.RegionSize;
                        stackSize = std::min<SIZE_T>(sizeof gSnapshotData, SIZE_T(end - context.Rsp));
                        std::memcpy(gSnapshotData, reinterpret_cast<void*>(context.Rsp), stackSize);
                        stackBase = context.Rsp;
                        captured = true;
                    }
                }
                ResumeThread(thread);
            }
            QString text;
            if (captured) {
                gSnapshotBase = stackBase;
                gSnapshotSize = stackSize;
                STACKFRAME64 frame{};
                frame.AddrPC.Offset = context.Rip;
                frame.AddrFrame.Offset = context.Rbp;
                frame.AddrStack.Offset = context.Rsp;
                frame.AddrPC.Mode = frame.AddrFrame.Mode = frame.AddrStack.Mode = AddrModeFlat;
                const HANDLE process = GetCurrentProcess();
                for (int i = 0; i < 60 && text.count(QLatin1Char('\n')) < 12; ++i) {
                    if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame, &context, readSnapshot,
                                     SymFunctionTableAccess64, SymGetModuleBase64, nullptr) ||
                        frame.AddrPC.Offset == 0) {
                        break;
                    }
                    alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256];
                    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
                    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
                    symbol->MaxNameLen = 255;
                    DWORD64 displacement = 0;
                    if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol)) {
                        text += QStringLiteral("       · ") + QString::fromLatin1(static_cast<const char*>(symbol->Name)).left(120) + QLatin1Char('\n');
                    }
                }
            }
            std::lock_guard<std::mutex> lock(samplesMutex_);
            samples_.emplace_back(currentStep_.load(), text);
#endif
        }
    });
}

void InterfaceCheck::stopWatchdog()
{
    watching_ = false;
    if (watchdog_.joinable()) {
        watchdog_.join();
    }
#ifdef _WIN32
    if (mainThread_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(mainThread_));
        mainThread_ = nullptr;
    }
#endif
}

void InterfaceCheck::expect(bool condition, const QString& what)
{
    if (current_ != nullptr && !condition) {
        current_->ok = false;
        current_->detail += (current_->detail.isEmpty() ? QString() : QStringLiteral("; ")) + what;
    }
}

void InterfaceCheck::blocking(const QString& what, const std::function<void()>& call)
{
    QElapsedTimer timer;
    timer.start();
    lastAction_ = what;
    heartbeat_ = nowMs();
    call();
    heartbeat_ = nowMs();
    if (current_ != nullptr && timer.elapsed() > current_->blockedMs) {
        current_->blockedMs = timer.elapsed();
        current_->blockedBy = what;
    }
}

void InterfaceCheck::settle(int ms)
{
    QElapsedTimer total;
    total.start();
    while (total.elapsed() < ms) {
        QElapsedTimer one;
        one.start();
        heartbeat_ = nowMs();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        heartbeat_ = nowMs();
        if (current_ != nullptr && one.elapsed() > current_->blockedMs) {
            current_->blockedMs = one.elapsed();  // un evento que paró la interfaz
            current_->blockedBy = QStringLiteral("un evento tras «%1»").arg(lastAction_);
        }
        QThread::msleep(5);
    }
}

bool InterfaceCheck::waitFor(const std::function<bool()>& condition, int timeoutMs)
{
    QElapsedTimer total;
    total.start();
    while (!condition()) {
        if (total.elapsed() > timeoutMs) {
            return false;
        }
        settle(20);
    }
    return true;
}

void InterfaceCheck::waitIdle()
{
    settle(400);  // la vista previa espera 250 ms tras el último cambio
    expect(waitFor([this] { return !window_.document_.computing(); }, 120000),
           QStringLiteral("el modelo no terminó de calcularse en segundo plano"));
    expect(waitFor([this] { return window_.tool_ == nullptr || !window_.tool_->previewing(); }, 120000),
           QStringLiteral("la vista previa no terminó de calcularse en segundo plano"));
    expect(waitFor([this] { return !viewBusy_; }, 60000), QStringLiteral("el visor no terminó de preparar el dibujo"));
    settle(100);
}

QDoubleSpinBox* InterfaceCheck::spin(int index) const
{
    if (window_.propertiesDock_ == nullptr || window_.propertiesDock_->widget() == nullptr) {
        return nullptr;
    }
    const auto boxes = window_.propertiesDock_->widget()->findChildren<QDoubleSpinBox*>();
    return index >= 0 && index < boxes.size() ? boxes[index] : nullptr;
}

QPushButton* InterfaceCheck::button(const QString& text) const
{
    if (window_.propertiesDock_ == nullptr || window_.propertiesDock_->widget() == nullptr) {
        return nullptr;
    }
    for (QPushButton* b : window_.propertiesDock_->widget()->findChildren<QPushButton*>()) {
        if (b->text() == text) {
            return b;
        }
    }
    return nullptr;
}

int InterfaceCheck::openChangeAccept(QAction* action, int spinIndex)
{
    const std::size_t before = window_.document_.featureCount();
    expect(action != nullptr && action->isEnabled(), QStringLiteral("el comando no está disponible"));
    if (action == nullptr || !action->isEnabled()) {
        return 0;
    }
    blocking(QStringLiteral("abrir la herramienta"), [&] { action->trigger(); });
    expect(window_.tool_ != nullptr, QStringLiteral("no se abrió la herramienta"));
    if (window_.tool_ == nullptr) {
        return 0;
    }
    waitIdle();
    if (QDoubleSpinBox* box = spin(spinIndex)) {
        blocking(QStringLiteral("cambiar un valor"), [&] { box->setValue(std::min(box->maximum(), box->value() * 1.1 + 0.05)); });
        waitIdle();
    } else {
        expect(false, QStringLiteral("no hay un cuadro numérico para cambiar"));
    }
    QPushButton* ok = button(QStringLiteral("Aceptar"));
    expect(ok != nullptr && ok->isEnabled(), QStringLiteral("«Aceptar» no está disponible"));
    if (ok != nullptr && ok->isEnabled()) {
        blocking(QStringLiteral("aceptar"), [&] { ok->click(); });
    }
    expect(waitFor([this] { return window_.tool_ == nullptr; }, 10000), QStringLiteral("la herramienta no se cerró al aceptar"));
    waitIdle();
    return int(window_.document_.featureCount() - before);
}

void InterfaceCheck::undoAll()
{
    while (window_.commands_.canUndo()) {
        blocking(QStringLiteral("deshacer"), [&] { window_.commands_.undo(); });
    }
    waitIdle();
    expect(window_.document_.featureCount() == 0, QStringLiteral("quedan operaciones tras deshacer"));
    expect(window_.document_.result().bodies.empty(), QStringLiteral("quedan cuerpos tras deshacer"));
    expect(window_.document_.parameters().empty(), QStringLiteral("quedan parámetros tras deshacer"));
}

void InterfaceCheck::step(const QString& name, const std::function<void()>& body)
{
    results_.push_back({name, true, {}, 0});
    current_ = &results_.back();
    currentStep_ = int(results_.size()) - 1;
    try {
        body();
    } catch (const std::exception& e) {
        expect(false, QStringLiteral("excepción: ") + QString::fromUtf8(e.what()));
    }
    if (current_->blockedMs > thresholdMs_) {
        current_->ok = false;
        current_->detail += (current_->detail.isEmpty() ? QString() : QStringLiteral("; ")) +
                            QStringLiteral("la interfaz se paró %1 ms (límite %2)").arg(current_->blockedMs).arg(thresholdMs_);
    }
    current_ = nullptr;
    currentStep_ = -1;
}

int InterfaceCheck::run()
{
    settle(300);
    step(QStringLiteral("La ventana empieza vacía"), [&] {
        expect(window_.document_.featureCount() == 0, QStringLiteral("el diseño no está vacío"));
        expect(window_.tool_ == nullptr, QStringLiteral("hay una herramienta abierta"));
    });

    // Cada generador: abrir, cambiar un valor, aceptar (queda todo), deshacer (no queda nada).
    const std::vector<QAction*> generators = window_.generatorActions_;
    for (QAction* action : generators) {
        step(QStringLiteral("Generador «%1»").arg(action->text()), [&] {
            const int added = openChangeAccept(action, 0);
            expect(added >= 1, QStringLiteral("no añadió operaciones"));
            waitIdle();
            expect(!window_.document_.result().bodies.empty(), QStringLiteral("no creó cuerpos"));
            undoAll();
        });
    }

    // Las herramientas con vista previa pesada o con colocación.
    const std::vector<std::pair<QString, QAction*>> tools = {{QStringLiteral("Rosca"), window_.threadAction_},
                                                             {QStringLiteral("Engranaje"), window_.gearAction_},
                                                             {QStringLiteral("Caja"), window_.boxAction_},
                                                             {QStringLiteral("Cilindro"), window_.cylinderAction_}};
    for (const auto& [name, action] : tools) {
        step(QStringLiteral("Herramienta «%1»: abrir, cambiar, aceptar y deshacer").arg(name), [&] {
            const int added = openChangeAccept(action, 2);
            expect(added == 1, QStringLiteral("debía añadir una operación"));
            waitIdle();
            expect(window_.document_.result().bodies.size() == 1, QStringLiteral("debía crear un cuerpo"));
            undoAll();
        });
    }
    step(QStringLiteral("Herramienta «Rosca»: cancelar no cambia nada"), [&] {
        blocking(QStringLiteral("abrir Rosca"), [&] { window_.threadAction_->trigger(); });
        expect(window_.tool_ != nullptr, QStringLiteral("no se abrió la herramienta"));
        waitIdle();
        if (QPushButton* cancel = button(QStringLiteral("Cancelar"))) {
            blocking(QStringLiteral("cancelar"), [&] { cancel->click(); });
        } else {
            expect(false, QStringLiteral("no hay botón «Cancelar»"));
        }
        expect(waitFor([this] { return window_.tool_ == nullptr; }, 10000), QStringLiteral("no se cerró al cancelar"));
        waitIdle();
        expect(window_.document_.featureCount() == 0, QStringLiteral("cancelar cambió el diseño"));
    });

    // Unidades: cambian los cuadros y el texto; se restauran al terminar.
    step(QStringLiteral("Unidades: centímetros y vuelta a milímetros"), [&] {
        const LengthUnit original = Units::instance().length();
        Units::instance().setLength(LengthUnit::Cm);
        settle(150);
        blocking(QStringLiteral("abrir Caja"), [&] { window_.boxAction_->trigger(); });
        waitIdle();
        if (QDoubleSpinBox* width = spin(3)) {
            expect(width->text().startsWith(QStringLiteral("2")), QStringLiteral("20 mm no se ve como 2 cm: «%1»").arg(width->text()));
        } else {
            expect(false, QStringLiteral("no hay cuadro de ancho"));
        }
        if (QPushButton* cancel = button(QStringLiteral("Cancelar"))) {
            cancel->click();
        }
        waitFor([this] { return window_.tool_ == nullptr; }, 10000);
        Units::instance().setLength(original);
        settle(150);
    });

    // La flecha de doble punta de la medida que se edita: al enfocar un cuadro deben verse píxeles turquesa.
    const auto arrowCheck = [&](const QString& name, QAction* action, int spinIndex, bool needsBody = false,
                                std::optional<double> preset = std::nullopt) {
        step(QStringLiteral("Flecha de la medida que se edita: %1").arg(name), [&] {
            if (needsBody) {
                expect(openChangeAccept(window_.boxAction_, 3) == 1, QStringLiteral("no se creó la caja de apoyo"));
            }
            blocking(QStringLiteral("abrir la herramienta"), [&] { action->trigger(); });
            waitIdle();
            const auto turquoise = [&] {
                const QImage image = window_.viewport_->grabFramebuffer();
                long count = 0;
                for (int y = 0; y < image.height(); ++y) {
                    for (int x = 0; x < image.width(); ++x) {
                        const QColor c = image.pixelColor(x, y);
                        if (c.red() < 140 && c.green() > 170 && c.blue() > 180 && c.green() - c.red() > 70) {
                            ++count;
                        }
                    }
                }
                return count;
            };
            QDoubleSpinBox* box = spin(spinIndex);
            expect(box != nullptr, QStringLiteral("no hay cuadro numérico"));
            if (box != nullptr && preset) {
                blocking(QStringLiteral("poner un valor"), [&] { box->setValue(*preset); });
                waitIdle();
            }
            // Como el usuario que se acerca a la pieza: una medida pequeña (el radio de una esquina) en una vista
            // lejana serían un par de píxeles.
            blocking(QStringLiteral("encuadrar"), [&] { window_.viewport_->fitAll(); });
            settle(300);
            if (box != nullptr) {
                window_.activateWindow();
                window_.propertiesDock_->widget()->setFocus();
                settle(200);
                const long without = turquoise();
                box->setFocus(Qt::OtherFocusReason);
                settle(400);
                const long with = turquoise();
                expect(box->hasFocus(), QStringLiteral("el cuadro no recibió el foco"));
                expect(with > without + 50, QStringLiteral("no se dibujó la flecha (píxeles turquesa: %1 sin foco, %2 con foco)").arg(without).arg(with));
                if (with <= without + 50) {
                    // Para ver qué pasó: la vista tal como quedó.
                    QString file = name;
                    file.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9]+")), QStringLiteral("_"));
                    window_.viewport_->grabFramebuffer().save(QDir::temp().filePath(QStringLiteral("flecha-%1.png").arg(file)));
                }
                // Al escribir otro valor la flecha sigue ahí (se recoloca con la vista previa).
                blocking(QStringLiteral("cambiar el valor"), [&] { box->setValue(std::min(box->maximum(), box->value() + 5.0)); });
                settle(500);
                const long changed = turquoise();
                expect(changed > 50, QStringLiteral("la flecha desapareció al cambiar el valor (%1 píxeles)").arg(changed));
                if (current_ != nullptr) {
                    current_->detail += QStringLiteral(" turquesa %1 → %2 → %3").arg(without).arg(with).arg(changed);
                }
            }
            if (QPushButton* cancel = button(QStringLiteral("Cancelar"))) {
                cancel->click();
            }
            waitFor([this] { return window_.tool_ == nullptr; }, 10000);
            waitIdle();
            if (needsBody) {
                undoAll();
            }
        });
    };
    arrowCheck(QStringLiteral("Caja"), window_.boxAction_, 3);
    arrowCheck(QStringLiteral("Cilindro"), window_.cylinderAction_, 3);
    arrowCheck(QStringLiteral("Rosca"), window_.threadAction_, 2);
    arrowCheck(QStringLiteral("Engranaje"), window_.gearAction_, 2);
    arrowCheck(QStringLiteral("Plano (ángulo)"), window_.planeAction_, 1, false, 30.0);
    arrowCheck(QStringLiteral("Mover cuerpo (X)"), window_.moveAction_, 0, true, 20.0);
    arrowCheck(QStringLiteral("Mover cuerpo (ángulo)"), window_.moveAction_, 3, true, 45.0);
    arrowCheck(QStringLiteral("Patrón (distancia)"), window_.patternAction_, 1, true);
    arrowCheck(QStringLiteral("Caja (radio de esquinas)"), window_.boxAction_, 6, false, 3.0);
    arrowCheck(QStringLiteral("Engranaje (ángulo de presión)"), window_.gearAction_, 5);
    arrowCheck(QStringLiteral("Engranaje (giro inicial)"), window_.gearAction_, 4, false, 30.0);
    arrowCheck(QStringLiteral("Engranaje (ángulo de hélice)"), window_.gearAction_, 8, false, 15.0);
    for (QAction* action : generators) {
        const QString name = action->text();
        if (name.contains(QStringLiteral("Caja con tapa")) || name.contains(QStringLiteral("Lightbox"))) {
            arrowCheck(name, action, 0);
        }
        if (name.contains(QStringLiteral("Par de engranajes"))) {
            arrowCheck(name + QStringLiteral(" (módulo)"), action, 0);
        }
    }

    // La vista previa de un generador es azul translúcida y, al terminar de calcular, no queda el «Calculando…».
    step(QStringLiteral("Vista previa translúcida de un generador"), [&] {
        QAction* lightbox = nullptr;
        for (QAction* action : generators) {
            if (action->text().contains(QStringLiteral("Lightbox"))) {
                lightbox = action;
            }
        }
        expect(lightbox != nullptr && lightbox->isEnabled(), QStringLiteral("no hay generador Lightbox"));
        if (lightbox == nullptr) {
            return;
        }
        blocking(QStringLiteral("abrir la herramienta"), [&] { lightbox->trigger(); });
        waitIdle();
        settle(300);
        const QImage image = window_.viewport_->grabFramebuffer();
        long bluish = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const QColor c = image.pixelColor(x, y);
                if (c.blue() > c.red() + 55 && c.blue() > c.green() + 25 && c.green() < 170) {
                    ++bluish;
                }
            }
        }
        expect(bluish > 500, QStringLiteral("la vista previa no se ve azul translúcida (%1 píxeles azulados)").arg(bluish));
        expect(window_.busyBadge_ != nullptr && !window_.busyBadge_->isVisible(), QStringLiteral("«Calculando…» sigue a la vista al terminar"));
        if (current_ != nullptr) {
            current_->detail += QStringLiteral(" azulados %1").arg(bluish);
        }
        if (QPushButton* cancel = button(QStringLiteral("Cancelar"))) {
            cancel->click();
        }
        waitFor([this] { return window_.tool_ == nullptr; }, 10000);
        waitIdle();
    });

    // Sección y medición con un cuerpo.
    step(QStringLiteral("Sección y medición con un cuerpo"), [&] {
        expect(openChangeAccept(window_.boxAction_, 3) == 1, QStringLiteral("no se creó la caja"));
        for (QAction* action : {window_.sectionAction_, window_.measureAction_}) {
            expect(action->isEnabled(), QStringLiteral("«%1» no está disponible con un cuerpo").arg(action->text()));
            blocking(QStringLiteral("activar"), [&] { action->trigger(); });
            expect(action->isChecked(), QStringLiteral("«%1» no se activó").arg(action->text()));
            waitIdle();
            blocking(QStringLiteral("desactivar"), [&] { action->trigger(); });
            expect(!action->isChecked(), QStringLiteral("«%1» no se desactivó").arg(action->text()));
        }
        undoAll();
    });

    // Un cilindro con una rosca cortante encima: cambiar el diámetro recalcula la rosca (lento), pero en segundo plano.
    step(QStringLiteral("Cambiar un cilindro con una rosca encima no cuelga la interfaz"), [&] {
        auto cylinder = std::make_unique<document::CylinderFeature>();
        cylinder->base.origin = document::OriginPlane::XY;
        cylinder->diameter = 10.0;
        cylinder->height = 10.0;
        auto thread = std::make_unique<document::ThreadFeature>();
        thread->base.origin = document::OriginPlane::XY;
        thread->spec.diameter = 8.0;
        thread->spec.pitch = 1.25;
        thread->spec.length = 10.0;
        thread->operation = document::Operation::Cut;
        blocking(QStringLiteral("agregar el cilindro y la rosca"), [&] {
            window_.commands_.push(std::make_unique<commands::AddFeature>(std::move(cylinder)));
            window_.commands_.push(std::make_unique<commands::AddFeature>(std::move(thread)));
        });
        waitIdle();
        const document::FeatureId cylinderId = window_.document_.feature(0).id();
        const document::FeatureId threadId = window_.document_.feature(1).id();
        expect(window_.document_.status(threadId).health != document::Health::Error,
               QStringLiteral("la rosca falló: %1").arg(QString::fromStdString(window_.document_.status(threadId).message)));
        for (const double diameter : {20.0, 10.0}) {
            auto edited = std::make_unique<document::CylinderFeature>(
                static_cast<const document::CylinderFeature&>(window_.document_.feature(0)));
            edited->diameter = diameter;
            QElapsedTimer timer;
            timer.start();
            blocking(QStringLiteral("cambiar el diámetro a %1").arg(diameter),
                     [&] { window_.commands_.push(std::make_unique<commands::EditFeature>(cylinderId, std::move(edited))); });
            expect(timer.elapsed() < 1000, QStringLiteral("cambiar el diámetro tardó %1 ms: debía volver al instante").arg(timer.elapsed()));
            expect(window_.document_.computing(), QStringLiteral("el recálculo no quedó en segundo plano"));
            settle(100);  // la interfaz sigue atendiendo eventos mientras se calcula
        }
        waitIdle();
        expect(window_.document_.status(threadId).health != document::Health::Error, QStringLiteral("la rosca falló al final"));
        undoAll();
    });

    step(QStringLiteral("Al final todo está limpio"), [&] {
        expect(window_.document_.featureCount() == 0, QStringLiteral("quedan operaciones"));
        expect(window_.commands_.isClean(), QStringLiteral("el diseño figura como modificado"));
        expect(window_.tool_ == nullptr, QStringLiteral("queda una herramienta abierta"));
    });

    // Informe.
    int failed = 0;
    qint64 worst = 0;
    QString text = QStringLiteral("Prueba de la interfaz — %1\nLímite de parada: %2 ms\n\n")
                       .arg(QDateTime::currentDateTime().toString(Qt::ISODate)).arg(thresholdMs_);
    for (const Result& r : results_) {
        failed += r.ok ? 0 : 1;
        worst = std::max(worst, r.blockedMs);
        text += QStringLiteral("%1  %2  (parada máx. %3 ms%4)%5\n")
                    .arg(r.ok ? QStringLiteral("OK   ") : QStringLiteral("FALLO"), r.name)
                    .arg(r.blockedMs)
                    .arg(r.blockedBy.isEmpty() ? QString() : QStringLiteral(", al ") + r.blockedBy)
                    .arg(r.detail.isEmpty() ? QString() : QStringLiteral("\n       → ") + r.detail);
    }
    {
        std::lock_guard<std::mutex> lock(samplesMutex_);
        if (!samples_.empty()) {
            text += QStringLiteral("\nParadas de más de un segundo (dónde estaba la interfaz):\n");
            int shown = 0;
            for (const auto& [index, stack] : samples_) {
                if (++shown > 12) {
                    text += QStringLiteral("  … y %1 más\n").arg(samples_.size() - 12);
                    break;
                }
                const QString where = index >= 0 && index < int(results_.size()) ? results_[std::size_t(index)].name : QStringLiteral("(entre pasos)");
                text += QStringLiteral("  • en «%1»:\n%2").arg(where, stack);
            }
        }
    }
    text += QStringLiteral("\n%1 pasos, %2 fallidos; la parada más larga de la interfaz fue de %3 ms.\n")
                .arg(results_.size()).arg(failed).arg(worst);
    QFile file(reportPath_);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);
        out.setGenerateByteOrderMark(true);
        out << text;
    }
    return failed == 0 ? 0 : 1;
}

} // namespace nuf::app
