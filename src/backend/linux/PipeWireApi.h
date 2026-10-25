#pragma once

#include <pipewire/pipewire.h>

#include <string>

namespace bcs::pw {

// libpipewire-0.3 entry points resolved with dlopen()/dlsym().
//
// Why not link directly: an AppImage that bundles libpipewire would load the
// HOST's PipeWire SPA plugins and client modules with a possibly different
// library version. Using the system library avoids that mismatch, and when
// PipeWire is absent the app still starts and reports the backend as
// unavailable instead of failing to launch.
struct Api {
    bool loaded = false;
    std::string error;

    void (*init)(int*, char***) = nullptr;
    const char* (*get_library_version)() = nullptr;
    pw_thread_loop* (*thread_loop_new)(const char*, const spa_dict*) = nullptr;
    void (*thread_loop_destroy)(pw_thread_loop*) = nullptr;
    int (*thread_loop_start)(pw_thread_loop*) = nullptr;
    void (*thread_loop_stop)(pw_thread_loop*) = nullptr;
    void (*thread_loop_lock)(pw_thread_loop*) = nullptr;
    void (*thread_loop_unlock)(pw_thread_loop*) = nullptr;
    pw_loop* (*thread_loop_get_loop)(pw_thread_loop*) = nullptr;
    pw_context* (*context_new)(pw_loop*, pw_properties*, size_t) = nullptr;
    void (*context_destroy)(pw_context*) = nullptr;
    pw_core* (*context_connect)(pw_context*, pw_properties*, size_t) = nullptr;
    int (*core_disconnect)(pw_core*) = nullptr;
    pw_properties* (*properties_new)(const char*, ...) = nullptr;
    pw_stream* (*stream_new)(pw_core*, const char*, pw_properties*) = nullptr;
    void (*stream_add_listener)(pw_stream*, spa_hook*, const pw_stream_events*, void*) = nullptr;
    int (*stream_connect)(pw_stream*, spa_direction, uint32_t, pw_stream_flags, const spa_pod**, uint32_t) = nullptr;
    pw_buffer* (*stream_dequeue_buffer)(pw_stream*) = nullptr;
    int (*stream_queue_buffer)(pw_stream*, pw_buffer*) = nullptr;
    void (*stream_destroy)(pw_stream*) = nullptr;
    const char* (*stream_state_as_string)(pw_stream_state) = nullptr;
};

// Loads once (thread-safe) and calls pw_init(). Check `loaded`.
const Api& api();

} // namespace bcs::pw
