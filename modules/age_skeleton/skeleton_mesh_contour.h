#pragma once
#include "core/variant/dictionary.h"

namespace SkeletonMeshContour {
Dictionary generate(const Dictionary &mesh, float threshold, float precision, int margin, String &error);
bool self_test();
}
