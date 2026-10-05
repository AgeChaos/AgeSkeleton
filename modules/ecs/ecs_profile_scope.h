#pragma once
#include "core/debugger/engine_debugger.h"
#include "core/os/os.h"
#include "core/os/thread.h"

// Engine profiler owns main-thread data. No sampling or allocation when disabled.
class ECSProfileScope {
#ifdef DEBUG_ENABLED
    const char *name;
    bool active;
    uint64_t started = 0;
#endif
public:
    inline static uint64_t samples = 0;
    static bool is_active() {
#ifdef DEBUG_ENABLED
        return Thread::is_main_thread() && EngineDebugger::is_profiling(SNAME("servers"));
#else
        return false;
#endif
    }
    explicit ECSProfileScope(const char *p_name) {
#ifdef DEBUG_ENABLED
        name = p_name;
        active = is_active();
        if (active) started = OS::get_singleton()->get_ticks_usec();
#endif
    }
    ~ECSProfileScope() {
#ifdef DEBUG_ENABLED
        if (active) {
            samples++;
            Array data; data.push_back("ECS CPU"); data.push_back(name);
            data.push_back(double(OS::get_singleton()->get_ticks_usec() - started) / 1000000.0);
            EngineDebugger::profiler_add_frame_data(SNAME("servers"), data);
        }
#endif
    }
};
