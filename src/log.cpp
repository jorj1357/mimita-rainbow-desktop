#include "log.h"
#include <ctime>
#include <direct.h>

FILE* Log::s_file = nullptr;
HANDLE Log::s_mutex = CreateMutexA(nullptr, FALSE, nullptr);

void Log::InitInstance(HINSTANCE hInstance) {
    char path[MAX_PATH];
    GetModuleFileNameA(hInstance, path, MAX_PATH);
    char* p = strrchr(path, '\\');
    if (p) *p = 0;
    p = strrchr(path, '\\');
    if (p) *p = 0;

    char log_dir[MAX_PATH];
    sprintf_s(log_dir, "%s\\logs", path);

    time_t raw = time(nullptr);
    struct tm t;
    localtime_s(&t, &raw);

    char day_dir[MAX_PATH];
    sprintf_s(day_dir, "%s\\%02d-%02d-%04d", log_dir, t.tm_mon + 1, t.tm_mday, t.tm_year + 1900);
    _mkdir(log_dir);
    _mkdir(day_dir);

    char file_path[MAX_PATH];
    sprintf_s(file_path, "%s\\log-%02d-%02d-%02d.txt", day_dir, t.tm_hour, t.tm_min, t.tm_sec);

    fopen_s(&s_file, file_path, "w");
    if (s_file) {
        fprintf(s_file, "=== desktop-fx log %04d-%02d-%02d %02d:%02d:%02d ===\n",
                t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
        fflush(s_file);
    }
}

void Log::Write(const char* fmt, ...) {
    WaitForSingleObject(s_mutex, INFINITE);

    char buf[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    OutputDebugStringA(buf);
    OutputDebugStringA("\n");

    if (s_file) {
        fprintf(s_file, "%s\n", buf);
        fflush(s_file);
    }

    ReleaseMutex(s_mutex);
}

void Log::HR(const char* label, HRESULT hr) {
    char buf[64];
    sprintf_s(buf, "0x%08X", hr);
    Write("%s -> %s", label, buf);
}

void Log::Close() {
    if (s_file) {
        fclose(s_file);
        s_file = nullptr;
    }
    if (s_mutex) {
        CloseHandle(s_mutex);
        s_mutex = nullptr;
    }
}
