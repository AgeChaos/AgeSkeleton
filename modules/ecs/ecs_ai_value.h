#pragma once
#ifdef TOOLS_ENABLED
#include "core/variant/variant.h"
class ECSAIValue {
public:
	static Variant encode(const Variant &value, int depth = 0);
	static Variant decode(const Variant &value, bool &ok, int depth = 0);
};
#endif
