#include "mpv/mpv_api.h"

#include <windows.h>

namespace ciniva::mpv {

static Api g_api;
static bool g_loaded = false;

const Api* load(const wchar_t* dll_path) {
    if (g_loaded) return &g_api;
    HMODULE lib = LoadLibraryExW(dll_path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!lib) return nullptr;
    Api a;
#define X(name)                                                              \
    a.name = reinterpret_cast<decltype(a.name)>(GetProcAddress(lib, #name)); \
    if (!a.name) {                                                           \
        FreeLibrary(lib);                                                    \
        return nullptr;                                                      \
    }
    CINIVA_MPV_FUNCTIONS(X)
#undef X
    if ((a.mpv_client_api_version() >> 16) != (MPV_CLIENT_API_VERSION >> 16)) {
        FreeLibrary(lib);
        return nullptr;
    }
    g_api = a;
    g_loaded = true;
    return &g_api;
}

const Api& api() { return g_api; }

}
