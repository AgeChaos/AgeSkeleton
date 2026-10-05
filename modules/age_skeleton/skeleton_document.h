#pragma once
#include "modules/ecs/ecs_scene.h"

namespace SkeletonDocument {
Error save(const Ref<ECSScene> &scene, const String &path, String &error);
Ref<ECSScene> load(const String &path, String &error);
}
