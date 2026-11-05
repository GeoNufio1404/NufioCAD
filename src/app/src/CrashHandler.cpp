#include "CrashHandler.hpp"

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <dbghelp.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

namespace nuf::app {

namespace {

std::wstring g_directory;

const char* describe(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
        return "violación de acceso a memoria";
    case EXCEPTION_STACK_OVERFLOW:
        return "desbordamiento de pila";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        return "división entera entre cero";
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        return "instrucción ilegal";
    case 0xE06D7363:
        return "excepción de C++ sin capturar";
    default:
        return "excepción";
    }
}

// «módulo + desplazamiento» de una dirección (sirve aunque no haya símbolos).
void moduleOf(HANDLE process, DWORD64 address, char* out, std::size_t size)
{
    HMODULE module = nullptr;
    char path[MAX_PATH] = {};
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(address), &module)
        && GetModuleFileNameA(module, path, MAX_PATH) > 0) {
        const char* name = std::strrchr(path, '\\');
        std::snprintf(out, size, "%s+0x%llx", name ? name + 1 : path,
                      static_cast<unsigned long long>(address - reinterpret_cast<DWORD64>(module)));
    } else {
        std::snprintf(out, size, "0x%llx", static_cast<unsigned long long>(address));
    }
    (void)process;
}

LONG WINAPI onCrash(EXCEPTION_POINTERS* info)
{
    static volatile LONG running = 0;
    if (InterlockedExchange(&running, 1) != 0) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    CreateDirectoryW(g_directory.c_str(), nullptr);

    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    wchar_t stamp[64];
    std::wcsftime(stamp, 64, L"cierre-%Y%m%d-%H%M%S", &local);
    const std::wstring base = g_directory + L"\\" + stamp;

    const HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, nullptr, TRUE);

    if (FILE* report = _wfopen((base + L".txt").c_str(), L"w")) {
        const DWORD code = info->ExceptionRecord->ExceptionCode;
        char where[600];
        moduleOf(process, reinterpret_cast<DWORD64>(info->ExceptionRecord->ExceptionAddress), where, sizeof(where));
        std::fprintf(report, "NufioCAD se cerró por un fallo\n%s (0x%08lx) en %s\n", describe(code),
                     static_cast<unsigned long>(code), where);
        if (code == EXCEPTION_ACCESS_VIOLATION && info->ExceptionRecord->NumberParameters >= 2) {
            std::fprintf(report, "Operación: %s en la dirección 0x%llx\n",
                         info->ExceptionRecord->ExceptionInformation[0] == 0 ? "lectura"
                         : info->ExceptionRecord->ExceptionInformation[0] == 1 ? "escritura" : "ejecución",
                         static_cast<unsigned long long>(info->ExceptionRecord->ExceptionInformation[1]));
        }
        std::fprintf(report, "\nPila de llamadas (de la más reciente a la más antigua):\n");

        CONTEXT context = *info->ContextRecord;
        STACKFRAME64 frame{};
        frame.AddrPC.Offset = context.Rip;
        frame.AddrFrame.Offset = context.Rbp;
        frame.AddrStack.Offset = context.Rsp;
        frame.AddrPC.Mode = frame.AddrFrame.Mode = frame.AddrStack.Mode = AddrModeFlat;
        alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 512];
        for (int i = 0; i < 64; ++i) {
            if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame, &context, nullptr,
                             SymFunctionTableAccess64, SymGetModuleBase64, nullptr)
                || frame.AddrPC.Offset == 0) {
                break;
            }
            char module[300];
            moduleOf(process, frame.AddrPC.Offset, module, sizeof(module));
            auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
            std::memset(symbol, 0, sizeof(SYMBOL_INFO));
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = 500;
            DWORD64 displacement = 0;
            std::fprintf(report, "%2d  %s", i, module);
            if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol)) {
                std::fprintf(report, "  %s", symbol->Name);
                IMAGEHLP_LINE64 line{};
                line.SizeOfStruct = sizeof(line);
                DWORD lineDisplacement = 0;
                if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &lineDisplacement, &line)) {
                    const char* file = std::strrchr(line.FileName, '\\');
                    std::fprintf(report, "  (%s:%lu)", file ? file + 1 : line.FileName, line.LineNumber);
                }
            }
            std::fprintf(report, "\n");
        }
        std::fclose(report);
    }

    const HANDLE dump = CreateFileW((base + L".dmp").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (dump != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION exception{GetCurrentThreadId(), info, FALSE};
        MiniDumpWriteDump(process, GetCurrentProcessId(), dump, MiniDumpWithIndirectlyReferencedMemory, &exception,
                          nullptr, nullptr);
        CloseHandle(dump);
    }
    return EXCEPTION_CONTINUE_SEARCH;  // Windows sigue con su aviso normal
}

} // namespace

void installCrashHandler()
{
    wchar_t appData[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH) == 0) {
        return;
    }
    // Misma carpeta que usa Qt para los datos de la aplicación.
    g_directory = std::wstring(appData) + L"\\NufioCAD\\NufioCAD";
    CreateDirectoryW((std::wstring(appData) + L"\\NufioCAD").c_str(), nullptr);
    CreateDirectoryW(g_directory.c_str(), nullptr);
    g_directory += L"\\cierres";
    SetUnhandledExceptionFilter(onCrash);
}

} // namespace nuf::app

#else

namespace nuf::app {

void installCrashHandler() {}

} // namespace nuf::app

#endif
