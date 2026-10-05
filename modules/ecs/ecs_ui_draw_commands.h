// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#pragma once
#include "core/math/color.h"
#include "core/templates/rid.h"
#include "core/variant/array.h"

namespace ECSUIDrawCommands {
bool validate(const Array &commands);
void draw(RID canvas, const Array &commands, const Color &modulate);
} //namespace ECSUIDrawCommands
