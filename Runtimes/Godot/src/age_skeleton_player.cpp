// SPDX-License-Identifier: LicenseRef-AgeSkeleton-Free-RestrictedCommercial-1.0
#include <godot_cpp/godot.hpp>
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include "ageskeleton/batcher.hpp"
using namespace godot;

class AgeSkeletonPlayer : public Node2D {
    GDCLASS(AgeSkeletonPlayer,Node2D)
    ageskeleton::Player player;
    ageskeleton::Batcher batcher;
    String source;
    double pixel_scale=1;
    Vector<Ref<Texture2D>> textures;
    Vector<RID> items;
    Vector<PackedVector2Array> vertices,uvs;
    Vector<PackedInt32Array> indices;
    void clear_items(){auto *rs=RenderingServer::get_singleton();for(RID item:items)rs->free_rid(item);items.clear();vertices.clear();uvs.clear();indices.clear();textures.clear();}
    static std::string utf8(const String &s){return s.utf8().get_data();}
    void redraw(){
        batcher.update(player);auto *rs=RenderingServer::get_singleton();
        while(items.size()<int(batcher.batches.size())) {RID item=rs->canvas_item_create();rs->canvas_item_set_parent(item,get_canvas_item());items.push_back(item);vertices.push_back(PackedVector2Array());uvs.push_back(PackedVector2Array());indices.push_back(PackedInt32Array());}
        for(int i=0;i<items.size();++i) {
            rs->canvas_item_clear(items[i]);rs->canvas_item_set_visible(items[i],i<int(batcher.batches.size()));
            if(i>=int(batcher.batches.size()))continue;
            const auto &b=batcher.batches[i];auto &points=vertices.write[i];auto &uv=uvs.write[i];auto &triangles=indices.write[i];PackedColorArray colors;
            int count=int(b.positions.size()/2);points.resize(count);uv.resize(count);colors.resize(count);triangles.resize(int(b.indices.size()));
            for(int v=0;v<count;++v){points.set(v,Vector2(b.positions[v*2],b.positions[v*2+1])*pixel_scale);uv.set(v,Vector2(b.uv[v*2],b.uv[v*2+1]));colors.set(v,Color(b.colors[v*4],b.colors[v*4+1],b.colors[v*4+2],b.colors[v*4+3]));}
            for(int k=0;k<triangles.size();++k)triangles.set(k,b.indices[k]);
            rs->canvas_item_set_draw_index(items[i],i);
            rs->canvas_item_add_triangle_array(items[i],triangles,points,colors,uv,PackedInt32Array(),PackedFloat32Array(),textures[b.texture]->get_rid());
        }
    }
protected:
    static void _bind_methods(){
        ClassDB::bind_method(D_METHOD("set_source","path"),&AgeSkeletonPlayer::set_source);ClassDB::bind_method(D_METHOD("get_source"),&AgeSkeletonPlayer::get_source);
        ADD_PROPERTY(PropertyInfo(Variant::STRING,"source",PROPERTY_HINT_FILE,"*.json"),"set_source","get_source");
        ClassDB::bind_method(D_METHOD("get_batch_count"),&AgeSkeletonPlayer::get_batch_count);
        ClassDB::bind_method(D_METHOD("load_file","path"),&AgeSkeletonPlayer::load_file);
        ClassDB::bind_method(D_METHOD("play","animation"),&AgeSkeletonPlayer::play);ClassDB::bind_method(D_METHOD("pause"),&AgeSkeletonPlayer::pause);ClassDB::bind_method(D_METHOD("resume"),&AgeSkeletonPlayer::resume);
        ClassDB::bind_method(D_METHOD("stop"),&AgeSkeletonPlayer::stop);ClassDB::bind_method(D_METHOD("seek","time"),&AgeSkeletonPlayer::seek);
        ClassDB::bind_method(D_METHOD("set_skin","name"),&AgeSkeletonPlayer::set_skin);ClassDB::bind_method(D_METHOD("set_wardrobe","group","skin"),&AgeSkeletonPlayer::set_wardrobe);
        ClassDB::bind_method(D_METHOD("set_slot_visible","slot","visible"),&AgeSkeletonPlayer::set_slot_visible);ClassDB::bind_method(D_METHOD("set_attachment","slot","key"),&AgeSkeletonPlayer::set_attachment);
        ClassDB::bind_method(D_METHOD("restore_attachment","slot"),&AgeSkeletonPlayer::restore_attachment);
        ClassDB::bind_method(D_METHOD("get_playback_time"),&AgeSkeletonPlayer::get_playback_time);ClassDB::bind_method(D_METHOD("is_playing"),&AgeSkeletonPlayer::is_playing);
        ClassDB::bind_method(D_METHOD("set_speed","value"),&AgeSkeletonPlayer::set_speed);ClassDB::bind_method(D_METHOD("get_speed"),&AgeSkeletonPlayer::get_speed);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"speed"),"set_speed","get_speed");
        ClassDB::bind_method(D_METHOD("set_ik_target","bone","target","chain_length","mix","iterations","tolerance"),&AgeSkeletonPlayer::set_ik_target,DEFVAL(2),DEFVAL(1.0),DEFVAL(24),DEFVAL(0.1));
        ClassDB::bind_method(D_METHOD("clear_ik_target","bone"),&AgeSkeletonPlayer::clear_ik_target);
        ClassDB::bind_method(D_METHOD("get_bone_tip","bone"),&AgeSkeletonPlayer::get_bone_tip);
        ClassDB::bind_method(D_METHOD("advance","delta"),&AgeSkeletonPlayer::advance);
        ADD_SIGNAL(MethodInfo("animation_event",PropertyInfo(Variant::STRING,"name"),PropertyInfo(Variant::DICTIONARY,"payload")));
    }
public:
    void _ready()override{if(items.is_empty()&&!source.is_empty())load_file(source);set_process(true);}
    void _process(double delta)override{if(player.playing)advance(delta);}
    bool advance(double delta){if(!player.update(delta))return false;redraw();for(const auto &e:player.events){Dictionary payload;payload["animation"]=String(e.animation.c_str());payload["time"]=e.time;payload["int"]=e.int_value;payload["float"]=e.float_value;payload["string"]=String(e.string_value.c_str());call_deferred("emit_signal","animation_event",String(e.name.c_str()),payload);}return true;}
    // Exported skeleton-local pixels, Y down; independent of node transform.
    bool set_ik_target(const String &bone,Vector2 target,int chain_length=2,double mix=1,int iterations=24,double tolerance=.1){bool ok=player.set_ik_target(utf8(bone),target.x,target.y,chain_length,float(mix),iterations,float(tolerance));if(ok)redraw();return ok;}
    bool clear_ik_target(const String &bone){bool ok=player.clear_ik_target(utf8(bone));if(ok)redraw();return ok;}
    Variant get_bone_tip(const String &bone)const{std::array<float,2> tip;if(!player.bone_tip(utf8(bone),tip))return Variant();return Vector2(tip[0],tip[1]);}
    void set_source(const String &path){source=path; if(is_inside_tree())load_file(path);}
    String get_source()const{return source;}
    bool load_file(const String &path){
        Ref<FileAccess> f=FileAccess::open(path,FileAccess::READ);if(f.is_null())return false;
        ageskeleton::Player next;std::string error;if(!next.load_json(utf8(f->get_as_text()),error)){UtilityFunctions::push_error(String(error.c_str()));return false;}
        Vector<Ref<Texture2D>> loaded;
        for(const auto &name:next.data().textures){Ref<Texture2D> tex=ResourceLoader::get_singleton()->load(path.get_base_dir().path_join(String(name.c_str())));if(tex.is_null())return false;loaded.push_back(tex);}
        clear_items();player=std::move(next);batcher=ageskeleton::Batcher();textures=loaded;source=path;
        redraw();return true;
    }
    int get_batch_count()const{return int(batcher.batches.size());}
    bool play(const String &name){bool ok=player.play(utf8(name));if(ok)redraw();return ok;}
    void pause(){player.playing=false;}void resume(){player.playing=true;}void stop(){player.stop();redraw();}
    bool seek(double time){bool ok=player.seek(float(time));if(ok)redraw();return ok;}
    bool set_skin(const String &name){bool ok=player.set_skin(utf8(name));if(ok)redraw();return ok;}
    bool set_wardrobe(const String &group,const String &name){bool ok=player.set_wardrobe(utf8(group),utf8(name));if(ok)redraw();return ok;}
    bool set_slot_visible(const String &name,bool show){bool ok=player.set_slot_visible(utf8(name),show);if(ok)redraw();return ok;}
    bool set_attachment(const String &slot,const String &key){bool ok=player.set_attachment(utf8(slot),utf8(key));if(ok)redraw();return ok;}
    bool restore_attachment(const String &slot){bool ok=player.set_attachment(utf8(slot),"",true);if(ok)redraw();return ok;}
    double get_playback_time()const{return player.time;}bool is_playing()const{return player.playing;}
    void set_speed(double value){if(std::isfinite(value))player.speed=float(value);}double get_speed()const{return player.speed;}
    ~AgeSkeletonPlayer(){clear_items();}
};
void initialize_ageskeleton(ModuleInitializationLevel level){if(level==MODULE_INITIALIZATION_LEVEL_SCENE)ClassDB::register_class<AgeSkeletonPlayer>();}
void terminate_ageskeleton(ModuleInitializationLevel){}
extern "C" GDExtensionBool GDE_EXPORT ageskeleton_library_init(GDExtensionInterfaceGetProcAddress get_proc,GDExtensionClassLibraryPtr library,GDExtensionInitialization *initialization){
    GDExtensionBinding::InitObject init(get_proc,library,initialization);init.register_initializer(initialize_ageskeleton);init.register_terminator(terminate_ageskeleton);init.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);return init.init();
}
