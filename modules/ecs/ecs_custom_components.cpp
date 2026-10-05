#include "ecs_custom_components.h"

#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/io/marshalls.h"
namespace ECSCustomComponents {
Dictionary registry() {
	const String path = "res://leanclr/ecs-components.json";
	if (!FileAccess::exists(path)) {
		return Dictionary();
	}
	Ref<JSON> json;
	json.instantiate();
	if (json->parse(FileAccess::get_file_as_string(path)) != OK || json->get_data().get_type() != Variant::DICTIONARY) {
		return Dictionary();
	}
	Dictionary root = json->get_data();
	Variant components = root.get("components", Dictionary());
	return components.get_type() == Variant::DICTIONARY ? Dictionary(components) : Dictionary();
}
bool is_component(const String &name) {
	return name.begins_with("ecs:");
}
String title(const String &name) {
	Dictionary schemas = registry();
	return schemas.has(name) ? String(Dictionary(schemas[name]).get("title", name)) : name;
}
Dictionary defaults(const Dictionary &schema) {
	Dictionary result;
	Array fields = schema.get("fields", Array());
	for (const Variant &raw : fields) {
		if (raw.get_type() != Variant::DICTIONARY) {
			continue;
		}
		Dictionary field = raw;
		String name = field.get("name", ""), kind = field.get("kind", "");
		if (kind == "bool") {
			result[name] = false;
		} else if (kind == "int" || kind == "long") {
			result[name] = int64_t(0);
		} else if (kind == "float" || kind == "double") {
			result[name] = 0.0;
		} else if (kind == "Godot.Vector2") {
			result[name] = Vector2();
		} else if (kind == "Godot.Vector3") {
			result[name] = Vector3();
		} else if (kind == "Godot.Vector4") {
			result[name] = Vector4();
		} else if (kind == "Godot.Color") {
			result[name] = Color(0, 0, 0, 0);
		} else if (kind == "Godot.Quaternion") {
			result[name] = Quaternion(0, 0, 0, 0);
		}
	}
	return result;
}
Dictionary with_defaults(const Dictionary &entity, const Dictionary &saved) {
	Dictionary result = entity.duplicate(true), schemas = saved.duplicate(true);
	schemas.merge(registry(), true);
	for (const Variant &key : result.keys()) {
		if (!is_component(String(key)) || !schemas.has(key) || result[key].get_type() != Variant::DICTIONARY) {
			continue;
		}
		Dictionary values = defaults(schemas[key]);
		values.merge(result[key], true);
		result[key] = values;
	}
	return result;
}
bool pack(const Dictionary &schema, const Dictionary &values, PackedByteArray &bytes) {
	int stride = schema.get("stride", 0);
	if (stride < 1 || stride > 65536 || schema.get("fields", Variant()).get_type() != Variant::ARRAY) {
		return false;
	}
	bytes.resize(stride);
	bytes.fill(0);
	Dictionary initial = defaults(schema);
	Array fields = schema["fields"];
	int cursor = 0;
	for (const Variant &raw : fields) {
		if (raw.get_type() != Variant::DICTIONARY) {
			return false;
		}
		Dictionary field = raw;
		String name = field.get("name", ""), kind = field.get("kind", "");
		int offset = field.get("offset", -1), size = field.get("size", 0);
		if (offset != cursor || size < 1 || offset > stride - size || !initial.has(name)) {
			return false;
		}
		cursor += size;
		Variant value = values.get(name, initial[name]);
		if (value.get_type() != initial[name].get_type() && !(initial[name].get_type() == Variant::FLOAT && value.get_type() == Variant::INT)) {
			return false;
		}
		uint8_t *target = bytes.ptrw() + offset;
		if (kind == "bool") {
			if (size != 1) {
				return false;
			}
			*target = bool(value) ? 1 : 0;
		} else if (kind == "int") {
			int64_t v = value;
			if (size != 4 || v < INT32_MIN || v > INT32_MAX) {
				return false;
			}
			encode_uint32(uint32_t(v), target);
		} else if (kind == "long") {
			if (size != 8) {
				return false;
			}
			encode_uint64(int64_t(value), target);
		} else if (kind == "float" || kind == "double") {
			double v = value;
			if (!Math::is_finite(v)) {
				return false;
			}
			if (kind == "float") {
				if (size != 4 || !Math::is_finite(float(v))) {
					return false;
				}
				encode_float(v, target);
			} else {
				if (size != 8) {
					return false;
				}
				encode_double(v, target);
			}
		} else {
			double parts[4] = {};
			int count = 0;
			if (kind == "Godot.Vector2") {
				Vector2 v = value;
				count = 2;
				parts[0] = v.x;
				parts[1] = v.y;
			} else if (kind == "Godot.Vector3") {
				Vector3 v = value;
				count = 3;
				parts[0] = v.x;
				parts[1] = v.y;
				parts[2] = v.z;
			} else if (kind == "Godot.Vector4") {
				Vector4 v = value;
				count = 4;
				for (int i = 0; i < 4; i++) {
					parts[i] = v[i];
				}
			} else if (kind == "Godot.Color") {
				Color v = value;
				count = 4;
				for (int i = 0; i < 4; i++) {
					parts[i] = v[i];
				}
			} else if (kind == "Godot.Quaternion") {
				Quaternion v = value;
				count = 4;
				for (int i = 0; i < 4; i++) {
					parts[i] = v[i];
				}
			}
			if (count == 0 || size != count * 4) {
				return false;
			}
			for (int i = 0; i < count; i++) {
				if (!Math::is_finite(float(parts[i]))) {
					return false;
				}
				encode_float(parts[i], target + i * 4);
			}
		}
	}
	return cursor == stride;
}
bool unpack(const Dictionary &schema,const PackedByteArray &bytes,Dictionary &values) {
	PackedByteArray layout;
	if(!pack(schema,Dictionary(),layout) || layout.size()!=bytes.size()) { return false; }
	Dictionary decoded;
	for(const Variant &raw:Array(schema["fields"])) {
		Dictionary field=raw; String name=field["name"],kind=field["kind"];
		int offset=field["offset"],size=field["size"]; const uint8_t *data=bytes.ptr()+offset;
		if(kind=="bool") { decoded[name]=bool(data[0]); }
		else if(kind=="int") { decoded[name]=int32_t(decode_uint32(data)); }
		else if(kind=="long") { decoded[name]=int64_t(decode_uint64(data)); }
		else if(kind=="float") { decoded[name]=decode_float(data); }
		else if(kind=="double") { decoded[name]=decode_double(data); }
		else {
			float v[4]={}; for(int i=0;i<size/4;i++) { v[i]=decode_float(data+i*4); }
			if(kind=="Godot.Vector2") { decoded[name]=Vector2(v[0],v[1]); }
			else if(kind=="Godot.Vector3") { decoded[name]=Vector3(v[0],v[1],v[2]); }
			else if(kind=="Godot.Vector4") { decoded[name]=Vector4(v[0],v[1],v[2],v[3]); }
			else if(kind=="Godot.Color") { decoded[name]=Color(v[0],v[1],v[2],v[3]); }
			else if(kind=="Godot.Quaternion") { decoded[name]=Quaternion(v[0],v[1],v[2],v[3]); }
		}
	}
	if(!pack(schema,decoded,layout)) { return false; }
	values=decoded; return true;
}
} //namespace ECSCustomComponents
