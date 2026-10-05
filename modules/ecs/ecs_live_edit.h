#pragma once
#ifdef TOOLS_ENABLED
#include "core/variant/variant.h"
namespace ECSLiveEdit {
Variant encode(const Variant &p_value, int p_depth = 0);
Variant decode(const Variant &p_value, bool &r_ok, int p_depth = 0);
}
#endif
