#pragma once

#include <mpv/client.h>

#define CINIVA_MPV_FUNCTIONS(X)  \
    X(mpv_client_api_version)    \
    X(mpv_error_string)          \
    X(mpv_free)                  \
    X(mpv_create)                \
    X(mpv_initialize)            \
    X(mpv_terminate_destroy)     \
    X(mpv_set_option_string)     \
    X(mpv_command)               \
    X(mpv_command_async)         \
    X(mpv_set_property)          \
    X(mpv_set_property_string)   \
    X(mpv_get_property)          \
    X(mpv_get_property_string)   \
    X(mpv_observe_property)      \
    X(mpv_event_name)            \
    X(mpv_wait_event)            \
    X(mpv_wakeup)                \
    X(mpv_set_wakeup_callback)

namespace ciniva::mpv {

struct Api {
#define X(name) decltype(&::name) name = nullptr;
    CINIVA_MPV_FUNCTIONS(X)
#undef X
};

const Api* load(const wchar_t* dll_path);
const Api& api();

}
