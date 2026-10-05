#ifdef TOOLS_ENABLED
#include "ecs_animation_editor.h"
#include "core/object/callable_mp.h"
#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/io/dir_access.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "scene/gui/check_box.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/item_list.h"
#include "servers/rendering/rendering_server.h"

namespace {
Ref<Image> read_frame(const String &path) {
    Ref<Image> image; image.instantiate();
    if(image->load_png_from_buffer(FileAccess::get_file_as_bytes(path))!=OK) { return Ref<Image>(); }
    return image;
}
void fourcc(const Ref<FileAccess> &f,const char *text) { f->store_buffer((const uint8_t *)text,4); }
uint64_t chunk(const Ref<FileAccess> &f,const char *tag) { fourcc(f,tag); uint64_t at=f->get_position(); f->store_32(0); return at; }
void end_chunk(const Ref<FileAccess> &f,uint64_t at) { uint64_t end=f->get_position(); f->seek(at); f->store_32(end-at-4); f->seek(end); if(end&1) { f->store_8(0); } }
// A fixed RGB palette reserves index zero for transparent pixels. Resetting LZW
// before the 9-bit dictionary fills makes the stream simple and deterministic.
Error write_gif(const String &path,const Vector<String> &frames,int fps) {
    Ref<Image> first=read_frame(frames[0]); if(first.is_null()) { return ERR_FILE_CORRUPT; }
    Ref<FileAccess> f=FileAccess::open(path,FileAccess::WRITE); if(f.is_null()) { return ERR_CANT_CREATE; }
    int w=first->get_width(),h=first->get_height();
    f->store_buffer((const uint8_t *)"GIF89a",6); f->store_16(w); f->store_16(h); f->store_8(0xf7); f->store_8(0); f->store_8(0);
    for(int i=0;i<256;i++) { f->store_8(((i>>5)&7)*255/7); f->store_8(((i>>2)&7)*255/7); f->store_8((i&3)*255/3); }
    const uint8_t loop[]={0x21,0xff,11,'N','E','T','S','C','A','P','E','2','.','0',3,1,0,0,0}; f->store_buffer(loop,sizeof(loop));
    for(int n=0;n<frames.size();n++) {
        Ref<Image> im=read_frame(frames[n]); if(im.is_null()) { return ERR_FILE_CORRUPT; }
        f->store_8(0x21); f->store_8(0xf9); f->store_8(4); f->store_8(9);
        f->store_16(MAX(1,int(Math::round((n+1)*100.0/fps)-Math::round(n*100.0/fps)))); f->store_8(0); f->store_8(0);
        f->store_8(0x2c); f->store_16(0); f->store_16(0); f->store_16(w); f->store_16(h); f->store_8(0); f->store_8(8);
        Vector<uint8_t> data; uint32_t bits=0; int count=0;
        auto code=[&](int c) { bits|=uint32_t(c)<<count; count+=9; while(count>=8) { data.push_back(bits&255); bits>>=8; count-=8; } };
        code(256); int run=0;
        for(int y=0;y<h;y++) { for(int x=0;x<w;x++) {
            if(run==200) { code(256); run=0; }
            Color color=im->get_pixel(x,y); int index=0;
            if(color.a>=.5) { index=(int(Math::round(color.r*7))<<5)|(int(Math::round(color.g*7))<<2)|int(Math::round(color.b*3)); if(index==0) { index=1; } }
            code(index); run++;
        } }
        code(257); if(count) { data.push_back(bits&255); }
        for(int offset=0;offset<data.size();offset+=255) { int size=MIN(255,data.size()-offset); f->store_8(size); f->store_buffer(data.ptr()+offset,size); } f->store_8(0);
    }
    f->store_8(0x3b); return f->get_error();
}
Error write_avi(const String &path,const Vector<String> &frames,int fps) {
    Ref<Image> first=read_frame(frames[0]); if(first.is_null()) { return ERR_FILE_CORRUPT; }
    int w=first->get_width(),h=first->get_height(),count=frames.size();
    Ref<FileAccess> f=FileAccess::open(path,FileAccess::WRITE); if(f.is_null()) { return ERR_CANT_CREATE; }
    auto riff=chunk(f,"RIFF"); fourcc(f,"AVI "); auto hdrl=chunk(f,"LIST"); fourcc(f,"hdrl");
    auto avih=chunk(f,"avih");
    for(uint32_t v:{uint32_t(1000000/fps),uint32_t(0),uint32_t(0),uint32_t(0x10),uint32_t(count),uint32_t(0),uint32_t(1),uint32_t(w*h*3),uint32_t(w),uint32_t(h),uint32_t(0),uint32_t(0),uint32_t(0),uint32_t(0)}) { f->store_32(v); } end_chunk(f,avih);
    auto strl=chunk(f,"LIST"); fourcc(f,"strl"); auto strh=chunk(f,"strh"); fourcc(f,"vids"); fourcc(f,"MJPG");
    f->store_32(0); f->store_16(0); f->store_16(0);
    for(uint32_t v:{uint32_t(0),uint32_t(1),uint32_t(fps),uint32_t(0),uint32_t(count),uint32_t(w*h*3),uint32_t(0xffffffff),uint32_t(0)}) { f->store_32(v); }
    f->store_16(0); f->store_16(0); f->store_16(w); f->store_16(h); end_chunk(f,strh);
    auto strf=chunk(f,"strf"); f->store_32(40); f->store_32(w); f->store_32(h); f->store_16(1); f->store_16(24); fourcc(f,"MJPG");
    f->store_32(w*h*3); for(int i=0;i<4;i++) { f->store_32(0); } end_chunk(f,strf); end_chunk(f,strl); end_chunk(f,hdrl);
    auto movi=chunk(f,"LIST"); auto base=f->get_position(); fourcc(f,"movi"); Vector<uint32_t> offsets,sizes;
    for(const String &frame:frames) {
        Ref<Image> im=read_frame(frame); if(im.is_null()) { return ERR_FILE_CORRUPT; } Vector<uint8_t> jpg=im->save_jpg_to_buffer(.9);
        if(jpg.is_empty() || f->get_position()+jpg.size()>1024ULL*1024*1024) { return ERR_OUT_OF_MEMORY; }
        offsets.push_back(f->get_position()-base); sizes.push_back(jpg.size()); auto block=chunk(f,"00dc"); f->store_buffer(jpg); end_chunk(f,block);
    }
    end_chunk(f,movi); auto idx=chunk(f,"idx1"); for(int i=0;i<count;i++) { fourcc(f,"00dc"); f->store_32(0x10); f->store_32(offsets[i]); f->store_32(sizes[i]); } end_chunk(f,idx); end_chunk(f,riff); return f->get_error();
}
}

void ECSAnimationEditor::start_motion_export(const String &path,int format) {
    if(export_animation->get_selected()<0) { export_status->set_text(String(U"请先创建或选择一个动画。")); return; }
    motion_saved_state=editing_state; motion_saved_time=time->get_value(); motion_saved_mode=animation_mode;
    editing_state=export_animation->get_selected_metadata(); Ref<Animation> clip=current_clip();
    if(clip.is_null() || clip->get_length()<=0) { editing_state=motion_saved_state; export_status->set_text(String(U"所选动画没有有效时长。")); return; }
    motion_fps=export_fps->get_value(); motion_count=int(Math::ceil(clip->get_length()*motion_fps));
    if(motion_count>1800) { editing_state=motion_saved_state; export_status->set_text(String(U"一次最多导出 1800 帧，请降低帧率或缩短动画。")); return; }
    motion_path=ProjectSettings::get_singleton()->globalize_path(path); motion_format=format; motion_index=0; motion_files.clear(); motion_size=Vector2i();
    motion_directory=motion_path.get_basename()+"_frames_"+itos(OS::get_singleton()->get_ticks_usec());
    if(DirAccess::make_dir_absolute(motion_directory)!=OK) { editing_state=motion_saved_state; export_status->set_text(String(U"无法创建帧缓存目录。")); return; }
    motion_exporting=true; motion_owner=owner->get_selected_id(); playing=false;
    asset_export_dialog->get_ok_button()->set_disabled(true);
    for(int i=0;i<export_formats->get_item_count();i++) { export_formats->set_item_disabled(i,true); }
    export_path->set_editable(false); export_scale->set_editable(false); export_fps->set_editable(false); export_animation->set_disabled(true); export_alpha->set_disabled(true);
    canvas->preview_animation(motion_owner,0,false,editing_state);
    RenderingServer::get_singleton()->connect("frame_post_draw",callable_mp(this,&ECSAnimationEditor::capture_motion_frame),CONNECT_ONE_SHOT|CONNECT_DEFERRED);
}
void ECSAnimationEditor::capture_motion_frame() {
    if(!motion_exporting) { return; }
    Ref<Image> image=prepare_export_image(); if(image.is_null()) { finish_motion_export(String(U"帧渲染失败。")); return; }
    if(motion_index==0) {
        motion_size=image->get_size();
        uint64_t pixels=uint64_t(motion_size.x)*motion_size.y*motion_count;
        if(pixels>256ULL*1024*1024 || (motion_format==5 && pixels>64ULL*1024*1024)) { finish_motion_export(String(U"导出总像素过大，请降低缩放或帧率。")); return; }
    }
    if(image->get_size()!=motion_size) { finish_motion_export(String(U"导出期间视口尺寸发生变化，请重新导出。")); return; }
    String file=motion_directory.path_join(vformat("frame_%05d.png",motion_index));
    motion_files.push_back(file);
    if(image->save_png(file)!=OK) { finish_motion_export(String(U"无法写入帧图片。")); return; } motion_index++;
    export_status->set_text(vformat(String(U"正在导出 %d / %d 帧；取消可中止。"),motion_index,motion_count));
    if(motion_index==motion_count) { finish_motion_export(); return; }
    canvas->preview_animation(motion_owner,motion_index/motion_fps,false,editing_state);
    RenderingServer::get_singleton()->connect("frame_post_draw",callable_mp(this,&ECSAnimationEditor::capture_motion_frame),CONNECT_ONE_SHOT|CONNECT_DEFERRED);
}
void ECSAnimationEditor::cancel_motion_export() { if(motion_exporting) { finish_motion_export(String(U"已取消导出。")); } }
void ECSAnimationEditor::finish_motion_export(const String &error) {
    auto callback=callable_mp(this,&ECSAnimationEditor::capture_motion_frame);
    if(RenderingServer::get_singleton()->is_connected("frame_post_draw",callback)) { RenderingServer::get_singleton()->disconnect("frame_post_draw",callback); }
    String failure=error; Error result=OK; String temp=motion_directory.path_join("output.partial");
    if(failure.is_empty()) {
        Dictionary manifest; manifest["animation"]=editing_state; manifest["fps"]=motion_fps; manifest["frame_count"]=motion_count; manifest["width"]=motion_size.x; manifest["height"]=motion_size.y; manifest["duration"]=motion_count/motion_fps;
        Array frames;
        if(motion_format==4) {
            for(const String &path:motion_files) { frames.push_back(motion_directory.get_file().path_join(path.get_file())); } manifest["frames"]=frames;
            Ref<FileAccess> f=FileAccess::open(temp,FileAccess::WRITE); if(f.is_null()) { result=ERR_CANT_CREATE; } else { f->store_string(JSON::stringify(manifest,"\t")); result=f->get_error(); }
        } else if(motion_format==5) {
            int columns=MAX(1,int(Math::ceil(Math::sqrt(double(motion_count)*motion_size.y/motion_size.x)))); int rows=(motion_count+columns-1)/columns;
            if(columns*motion_size.x>16384 || rows*motion_size.y>16384) { result=ERR_OUT_OF_MEMORY; }
            else {
                Ref<Image> atlas=Image::create_empty(columns*motion_size.x,rows*motion_size.y,false,Image::FORMAT_RGBA8); atlas->fill(Color(0,0,0,0));
                for(int i=0;i<motion_files.size();i++) { Ref<Image> im=read_frame(motion_files[i]); if(im.is_null()) { result=ERR_FILE_CORRUPT; break; } Vector2i pos((i%columns)*motion_size.x,(i/columns)*motion_size.y); atlas->blit_rect(im,Rect2i(Vector2i(),motion_size),pos); Dictionary entry; entry["x"]=pos.x; entry["y"]=pos.y; entry["w"]=motion_size.x; entry["h"]=motion_size.y; entry["duration"]=1.0/motion_fps; frames.push_back(entry); }
                if(result==OK) { result=atlas->save_png(temp); } manifest["frames"]=frames; manifest["image"]=motion_path.get_file();
                if(result==OK) { auto f=FileAccess::open(motion_directory.path_join("metadata.partial"),FileAccess::WRITE); if(f.is_null()) { result=ERR_CANT_CREATE; } else { f->store_string(JSON::stringify(manifest,"\t")); result=f->get_error(); } }
            }
        } else if(motion_format==6) { result=write_gif(temp,motion_files,int(motion_fps)); }
        else { result=write_avi(temp,motion_files,int(motion_fps)); }
        if(result==OK) { result=DirAccess::rename_absolute(temp,motion_path); }
        if(result==OK && motion_format==5) { result=DirAccess::rename_absolute(motion_directory.path_join("metadata.partial"),motion_path+".json"); }
        if(result!=OK) { failure=String(U"导出写入失败：")+itos(result); }
    }
    if(FileAccess::exists(temp)) { DirAccess::remove_absolute(temp); }
    if(FileAccess::exists(motion_directory.path_join("metadata.partial"))) { DirAccess::remove_absolute(motion_directory.path_join("metadata.partial")); }
    if(motion_format!=4 || !failure.is_empty()) { for(const String &path:motion_files) { DirAccess::remove_absolute(path); } DirAccess::remove_absolute(motion_directory); }
    motion_exporting=false; editing_state=motion_saved_state;
    if(motion_saved_mode) { seek(motion_saved_time); } else { canvas->stop_animation_preview(); }
    asset_export_dialog->get_ok_button()->set_disabled(false); export_path->set_editable(true); export_scale->set_editable(true); export_fps->set_editable(true); export_animation->set_disabled(false);
    for(int i=0;i<export_formats->get_item_count();i++) { export_formats->set_item_disabled(i,false); }
    export_format_changed(motion_format);
    export_status->set_text(failure.is_empty()?String(U"已导出：")+motion_path:failure);
    if(failure.is_empty() && export_open->is_pressed()) { OS::get_singleton()->shell_show_in_file_manager(motion_path); }
}
#endif
