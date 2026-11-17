#include "StallWatch.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <mutex>

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#endif

namespace nuf::app {

#ifdef _WIN32

namespace {

// La copia de la pila del hilo parado, para recorrerla sin tocarla.
DWORD64 gSnapshotBase = 0;
SIZE_T gSnapshotSize = 0;
unsigned char gSnapshotData[256 * 1024];
std::mutex gSampleMutex;  // una captura a la vez (la copia es compartida)

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

void* currentThreadHandle()
{
    HANDLE self = nullptr;
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &self, 0, FALSE, DUPLICATE_SAME_ACCESS);
    // Los símbolos se cargan aquí, en el hilo de la interfaz y con calma, no con él parado.
    static std::once_flag once;
    std::call_once(once, [] {
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        SymInitialize(GetCurrentProcess(), nullptr, TRUE);
    });
    return self;
}

void releaseThreadHandle(void* handle)
{
    if (handle != nullptr) {
        CloseHandle(static_cast<HANDLE>(handle));
    }
}

QString sampleThreadStack(void* handle, int maxLines)
{
    if (handle == nullptr) {
        return {};
    }
    std::lock_guard<std::mutex> lock(gSampleMutex);
    // Se detiene el hilo un instante, solo para copiar sus registros y su pila (sin tocar el montón ni cargar
    // símbolos: el hilo parado podría tenerlos bloqueados), y se le deja seguir; la pila se descifra después.
    HANDLE thread = static_cast<HANDLE>(handle);
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
    if (!captured) {
        return text;
    }
    gSnapshotBase = stackBase;
    gSnapshotSize = stackSize;
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrPC.Mode = frame.AddrFrame.Mode = frame.AddrStack.Mode = AddrModeFlat;
    const HANDLE process = GetCurrentProcess();
    for (int i = 0; i < 60 && text.count(QLatin1Char('\n')) < maxLines; ++i) {
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
    return text;
}

#else

void* currentThreadHandle() { return nullptr; }
void releaseThreadHandle(void*) {}
QString sampleThreadStack(void*, int) { return {}; }

#endif

namespace {

qint64 nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace

int StallWatch::thresholdFromEnvironment()
{
    bool ok = false;
    const int value = qEnvironmentVariableIntValue("NUF_VIGILANTE", &ok);
    if (!ok || value <= 0) {
        return 0;
    }
    return value == 1 ? 200 : value;
}

QString StallWatch::defaultDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/cierres");
}

StallWatch::StallWatch(int thresholdMs, const QString& directory)
    : thresholdMs_(std::max(50, thresholdMs))
{
    QDir().mkpath(directory);
    reportPath_ = QDir(directory).filePath(QStringLiteral("paradas.txt"));
    mainThread_ = currentThreadHandle();
    heartbeat_ = nowMs();
    // El latido lo da el propio bucle de eventos: si algo lo ocupa, el latido se retrasa.
    auto* beat = new QTimer(QCoreApplication::instance());
    beat->setTimerType(Qt::PreciseTimer);
    beat->setInterval(25);
    QObject::connect(beat, &QTimer::timeout, beat, [this] { heartbeat_ = nowMs(); });
    beat->start();

    if (QFile file(reportPath_); file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);
        out << "\n=== Sesión " << QDateTime::currentDateTime().toString(Qt::ISODate) << " · umbral " << thresholdMs_ << " ms ===\n";
    }
    running_ = true;
    thread_ = std::thread([this] { run(); });
}

StallWatch::~StallWatch()
{
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
    releaseThreadHandle(mainThread_);
}

void StallWatch::run()
{
    bool stalled = false;
    qint64 stallStart = 0;  // el último latido antes de la parada
    QString stack;
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const qint64 beat = heartbeat_.load();
        if (!stalled) {
            if (nowMs() - beat >= thresholdMs_) {
                stalled = true;
                stallStart = beat;
                stack = sampleThreadStack(mainThread_);
            }
        } else if (beat != stallStart) {
            report(beat - stallStart, stack);
            stalled = false;
            stack.clear();
        }
    }
}

void StallWatch::report(qint64 durationMs, const QString& stack)
{
    ++stalls_;
    QFile file(reportPath_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")) << "  parada de " << durationMs << " ms\n"
        << (stack.isEmpty() ? QStringLiteral("       (sin pila: solo se captura en Windows)\n") : stack);
}

} // namespace nuf::app
