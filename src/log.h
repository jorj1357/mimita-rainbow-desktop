#pragma once
#include <windows.h>
#include <cstdio>
#include <string>

class Log {
public:
    static void InitInstance(HINSTANCE hInstance);
    static void Write(const char* fmt, ...);
    static void HR(const char* label, HRESULT hr);
    static void Close();

private:
    static FILE* s_file;
    static HANDLE s_mutex;
};
