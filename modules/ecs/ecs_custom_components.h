// AgeChaos 自定义组件元数据与跨平台字段编码。
#pragma once
#include "core/variant/variant.h"
namespace ECSCustomComponents {
Dictionary registry();
bool is_component(const String &name);
Dictionary defaults(const Dictionary &schema);
Dictionary with_defaults(const Dictionary &entity, const Dictionary &saved);
String title(const String &name);
bool pack(const Dictionary &schema, const Dictionary &values, PackedByteArray &bytes);
bool unpack(const Dictionary &schema,const PackedByteArray &bytes,Dictionary &values);
} //namespace ECSCustomComponents
