#ifdef TOOLS_ENABLED
#include "ecs_ai_value.h"
#include "core/object/class_db.h"
#include "core/io/resource_loader.h"
Variant ECSAIValue::encode(const Variant &v, int depth) {
    if(depth > 32) {Dictionary limit;limit["opaque"]=true;limit["reason"]="Container nesting exceeds 32 levels";return limit;}
    Dictionary out; Array values;
    switch (v.get_type()) {
        case Variant::VECTOR2: { Vector2 x=v; values.push_back(x.x);values.push_back(x.y); break; }
        case Variant::VECTOR3: { Vector3 x=v; values.push_back(x.x);values.push_back(x.y);values.push_back(x.z); break; }
        case Variant::COLOR: { Color x=v; values.push_back(x.r);values.push_back(x.g);values.push_back(x.b);values.push_back(x.a); break; }
        case Variant::OBJECT: { Object *object=v; if (!object) return Variant(); Resource *r=Object::cast_to<Resource>(object); out["type"]="Resource"; out["path"]=r ? r->get_path() : ""; out["class"]=object->get_class(); return out; }
        case Variant::ARRAY: { Array a=v, result; for(int i=0;i<a.size();i++) result.push_back(encode(a[i],depth+1)); if(!a.is_typed())return result;out["type"]="Array";out["items"]=result;out["element_type"]=int(a.get_typed_builtin());out["element_class"]=a.get_typed_class_name();if(!a.get_typed_script().is_null()){out["opaque"]=true;out["reason"]="Script-constrained array";}return out; }
        case Variant::DICTIONARY: { Dictionary d=v, result; Array keys=d.keys(); bool plain=!d.is_typed() && !d.has("type") && !d.has("opaque");for(int i=0;i<keys.size();i++)plain=plain && keys[i].get_type()==Variant::STRING;
            if(plain){for(int i=0;i<keys.size();i++)result[keys[i]]=encode(d[keys[i]],depth+1);return result;}
            Array entries;for(int i=0;i<keys.size();i++){Array pair;pair.push_back(encode(keys[i],depth+1));pair.push_back(encode(d[keys[i]],depth+1));entries.push_back(pair);}
            out["type"]="Dictionary";out["entries"]=entries;
            out["key_type"]=int(d.get_typed_key_builtin());out["value_type"]=int(d.get_typed_value_builtin());
            out["key_class"]=d.get_typed_key_class_name();out["value_class"]=d.get_typed_value_class_name();
            if(!d.get_typed_key_script().is_null() || !d.get_typed_value_script().is_null()){out["opaque"]=true;out["reason"]="Script-constrained dictionary";}return out; }
        case Variant::NODE_PATH: out["type"]="NodePath";out["value"]=String(NodePath(v));return out;
        case Variant::STRING_NAME:out["type"]="StringName";out["value"]=String(v);return out;
        default:
            if(v.get_type()<=Variant::STRING) return v;
            out["type"]=Variant::get_type_name(v.get_type());
            if(v.get_type()==Variant::RID || v.get_type()==Variant::CALLABLE || v.get_type()==Variant::SIGNAL) {out["opaque"]=true;out["text"]=String(v);return out;}
            if(v.get_type()>=Variant::PACKED_BYTE_ARRAY) {Array items;for(uint64_t i=0;i<v.get_indexed_size();i++){bool valid=false,oob=false;items.push_back(encode(v.get_indexed(i,valid,oob),depth+1));}out["items"]=items;return out;}
            List<StringName> members;Variant::get_member_list(v.get_type(),&members);Dictionary fields;
            for(const StringName &name:members){bool valid=false;fields[name]=encode(v.get_named(name,valid),depth+1);}out["fields"]=fields;return out;
    }
    out["type"]=Variant::get_type_name(v.get_type()); out["value"]=values; return out;
}
Variant ECSAIValue::decode(const Variant &v, bool &ok, int depth) {
    if(!ok || depth > 32) {ok=false;return Variant();}
    if (v.get_type()==Variant::ARRAY) {Array input=v,result;for(int i=0;i<input.size() && ok;i++)result.push_back(decode(input[i],ok,depth+1));return result;}
    if (v.get_type()!=Variant::DICTIONARY) return v;
    Dictionary d=v; String type=d.get("type", "");
    if(bool(d.get("opaque",false))){ok=false;return Variant();}
    auto valid_type=[](int id,const StringName &cls){return id>=Variant::NIL && id<Variant::VARIANT_MAX && (cls==StringName() || (id==Variant::OBJECT && ClassDB::class_exists(cls)));};
    auto accepts=[](Variant &value,int id,const StringName &cls){if(id==Variant::NIL)return true;if(id==Variant::INT && value.get_type()==Variant::FLOAT){double n=value;if(!Math::is_finite(n) || n < -9223372036854775808.0 || n >= 9223372036854775808.0 || n!=Math::floor(n))return false;value=int64_t(n);}if(id==Variant::FLOAT && value.get_type()==Variant::INT)value=double(value);if(id==Variant::OBJECT){if(value.get_type()!=Variant::OBJECT && value.get_type()!=Variant::NIL)return false;Object *object=value;return value.get_type()==Variant::NIL || (value.get_type()==Variant::OBJECT && (!object || cls==StringName() || object->is_class(cls)));}return value.get_type()==id;};
    if(type=="Array" && d.has("element_type")){
        int id=d.get("element_type",0);StringName cls=d.get("element_class",StringName());ok=valid_type(id,cls) && d.get("items",Variant()).get_type()==Variant::ARRAY;if(!ok)return Variant();
        Array result,input=d["items"];result.set_typed(id,cls,Variant());for(int i=0;i<input.size() && ok;i++){Variant item=decode(input[i],ok,depth+1);ok=ok && accepts(item,id,cls);if(ok)result.push_back(item);}return result;
    }
    if(type=="Dictionary" && d.has("entries")){
        int kt=d.get("key_type",0),vt=d.get("value_type",0);StringName kc=d.get("key_class",StringName()),vc=d.get("value_class",StringName());ok=valid_type(kt,kc) && valid_type(vt,vc) && d["entries"].get_type()==Variant::ARRAY;if(!ok)return Variant();
        Dictionary result;result.set_typed(kt,kc,Variant(),vt,vc,Variant());Array entries=d["entries"];
        for(int i=0;i<entries.size() && ok;i++){ok=entries[i].get_type()==Variant::ARRAY;if(!ok)break;Array pair=entries[i];ok=pair.size()==2;if(!ok)break;Variant key=decode(pair[0],ok,depth+1),value=decode(pair[1],ok,depth+1);ok=ok && accepts(key,kt,kc) && accepts(value,vt,vc) && !result.has(key);if(ok)result[key]=value;}return result;
    }
    if(type=="int") {
        Variant number=d.get("value",Variant());
        ok=(number.get_type()==Variant::INT || number.get_type()==Variant::FLOAT) && Math::is_finite(double(number)) && Math::floor(double(number))==double(number) && Math::abs(double(number))<=9007199254740991.0;
        return ok?Variant(int64_t(number)):Variant();
    }
    if(type=="StringName") return StringName(String(d.get("value","")));
    if(d.has("fields") || d.has("items")) {
        for(int id=Variant::VECTOR2;id<Variant::VARIANT_MAX;id++) {
            if(Variant::get_type_name(Variant::Type(id))!=type)continue;
            if(id==Variant::OBJECT || id==Variant::RID || id==Variant::CALLABLE || id==Variant::SIGNAL){ok=false;return Variant();}
            Variant result;Callable::CallError error;Variant::construct(Variant::Type(id),result,nullptr,0,error);ok=error.error==Callable::CallError::CALL_OK;
            if(d.has("fields") && d["fields"].get_type()==Variant::DICTIONARY){Dictionary fields=d["fields"];Array keys=fields.keys();for(int i=0;i<keys.size() && ok;i++){Variant item=decode(fields[keys[i]],ok,depth+1);if(ok)result.set_named(keys[i],item,ok);}}
            else if(d.has("items") && d["items"].get_type()==Variant::ARRAY){Array input=d["items"],items;for(int i=0;i<input.size() && ok;i++)items.push_back(decode(input[i],ok,depth+1));Variant arg=items;const Variant *ptr=&arg;if(ok)Variant::construct(Variant::Type(id),result,&ptr,1,error);ok=ok && error.error==Callable::CallError::CALL_OK;}
            else ok=false;
            return result;
        }ok=false;return Variant();
    }
    if(type=="Resource") { String path=d.get("path", ""); if(!path.begins_with("res://") || path.contains("..")) {ok=false;return Variant();} Ref<Resource> r=ResourceLoader::load(path);ok=r.is_valid();return r; }
    if(type.is_empty()) { Dictionary result;Array keys=d.keys();for(int i=0;i<keys.size() && ok;i++)result[keys[i]]=decode(d[keys[i]],ok,depth+1);return result; }
    if(d.has("args")) {
        if(d["args"].get_type()!=Variant::ARRAY) {ok=false;return Variant();}
        Array input=d["args"];if(input.size()>32) {ok=false;return Variant();}
        Variant values[32];const Variant *ptrs[32];
        for(int i=0;i<input.size() && ok;i++) {values[i]=decode(input[i],ok,depth+1);ptrs[i]=&values[i];}
        for(int type_id=Variant::BOOL;type_id<Variant::VARIANT_MAX && ok;type_id++) {
            if(type_id==Variant::OBJECT || type_id==Variant::CALLABLE || type_id==Variant::SIGNAL || type_id==Variant::RID) continue;
            if(Variant::get_type_name(Variant::Type(type_id))==type) {Variant result;Callable::CallError error;Variant::construct(Variant::Type(type_id),result,ptrs,input.size(),error);ok=error.error==Callable::CallError::CALL_OK;return result;}
        }
        ok=false;return Variant();
    }
    if(type=="NodePath") return NodePath(String(d.get("value", "")));
    Variant list=d.get("value", Variant()); if(list.get_type()!=Variant::ARRAY) {ok=false;return Variant();} Array a=list;
    for(int i=0;i<a.size();i++) if(a[i].get_type()!=Variant::INT && a[i].get_type()!=Variant::FLOAT) {ok=false;return Variant();}
    if(type=="Vector2" && a.size()==2) return Vector2(a[0],a[1]);
    if(type=="Vector3" && a.size()==3) return Vector3(a[0],a[1],a[2]);
    if(type=="Color" && a.size()==4) return Color(a[0],a[1],a[2],a[3]);
    ok=false;return Variant();
}

#endif