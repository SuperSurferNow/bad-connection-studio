#include "backend/linux/PipeWireApi.h"

#include <dlfcn.h>

#include <mutex>

namespace bcs::pw {

namespace {

template <typename Fn>
bool resolve(void* lib, const char* name, Fn& out, std::string& err)
{
    out = reinterpret_cast<Fn>(dlsym(lib, name));
    if (!out) {
        err = std::string("libpipewire-0.3 is missing symbol ") + name;
        return false;
    }
    return true;
}

Api load()
{
    Api a;
    void* lib = dlopen("libpipewire-0.3.so.0", RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        const char* e = dlerror();
        a.error = std::string("PipeWire client library not found (libpipewire-0.3.so.0): ") + (e ? e : "");
        return a;
    }
    // Intentionally never dlclose()d: PipeWire registers atexit handlers.
    bool ok = resolve(lib, "pw_init", a.init, a.error) &&
              resolve(lib, "pw_get_library_version", a.get_library_version, a.error) &&
              resolve(lib, "pw_thread_loop_new", a.thread_loop_new, a.error) &&
              resolve(lib, "pw_thread_loop_destroy", a.thread_loop_destroy, a.error) &&
              resolve(lib, "pw_thread_loop_start", a.thread_loop_start, a.error) &&
              resolve(lib, "pw_thread_loop_stop", a.thread_loop_stop, a.error) &&
              resolve(lib, "pw_thread_loop_lock", a.thread_loop_lock, a.error) &&
              resolve(lib, "pw_thread_loop_unlock", a.thread_loop_unlock, a.error) &&
              resolve(lib, "pw_thread_loop_get_loop", a.thread_loop_get_loop, a.error) &&
              resolve(lib, "pw_context_new", a.context_new, a.error) &&
              resolve(lib, "pw_context_destroy", a.context_destroy, a.error) &&
              resolve(lib, "pw_context_connect", a.context_connect, a.error) &&
              resolve(lib, "pw_core_disconnect", a.core_disconnect, a.error) &&
              resolve(lib, "pw_properties_new", a.properties_new, a.error) &&
              resolve(lib, "pw_stream_new", a.stream_new, a.error) &&
              resolve(lib, "pw_stream_add_listener", a.stream_add_listener, a.error) &&
              resolve(lib, "pw_stream_connect", a.stream_connect, a.error) &&
              resolve(lib, "pw_stream_dequeue_buffer", a.stream_dequeue_buffer, a.error) &&
              resolve(lib, "pw_stream_queue_buffer", a.stream_queue_buffer, a.error) &&
              resolve(lib, "pw_stream_destroy", a.stream_destroy, a.error) &&
              resolve(lib, "pw_stream_state_as_string", a.stream_state_as_string, a.error);
    if (!ok)
        return a;
    a.init(nullptr, nullptr);
    a.loaded = true;
    return a;
}

} // namespace

const Api& api()
{
    static std::once_flag once;
    static Api instance;
    std::call_once(once, [] { instance = load(); });
    return instance;
}

} // namespace bcs::pw
