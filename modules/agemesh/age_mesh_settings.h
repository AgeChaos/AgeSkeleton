// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#pragma once
#include "core/config/project_settings.h"
#include "core/os/os.h"

namespace AgeMeshSettings {
inline int export_mode() {
    if (OS::get_singleton()->has_feature("agemesh_export_disabled")) { return 1; }
    if (OS::get_singleton()->has_feature("agemesh_export_cpu")) { return 2; }
    if (OS::get_singleton()->has_feature("agemesh_export_gpu")) { return 3; }
    return 0;
}
inline bool enabled() {
    int mode = export_mode();
    return mode ? mode != 1 : bool(GLOBAL_GET("rendering/agemesh/enabled"));
}
inline bool gpu_enabled() {
    int mode = export_mode();
    return mode ? mode == 3 : bool(GLOBAL_GET("rendering/agemesh/gpu_culling"));
}
}
