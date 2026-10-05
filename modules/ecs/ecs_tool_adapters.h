#pragma once
#include "core/variant/variant.h"

namespace ECSToolAdapters {
// Preserve source entity indices; expanded grid cells are appended.
bool compile(Array &r_entities, String &r_error);
bool test();
}
