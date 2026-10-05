#pragma once
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

namespace ECSUIComponents {
bool is_component(const String &p_name);
String title(const String &p_name);
Dictionary defaults(const String &p_name);
Dictionary migrate(const Dictionary &p_entity);
bool has_ui(const Dictionary &p_entity);
bool compose(const Dictionary &p_entity, Dictionary &r_ui);
Dictionary preset(const String &p_kind);
bool add(Dictionary &p_entity, const String &p_component);
void remove(Dictionary &p_entity, const String &p_component);
} //namespace ECSUIComponents
