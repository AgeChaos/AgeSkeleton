#include "skeleton_mesh_contour.h"
#ifdef TOOLS_ENABLED
#include "ecs_skeleton_editor_plugin.h"
#include "ecs_animation_editor.h"
#include "core/config/project_settings.h"
#include "core/io/resource_loader.h"
#include "core/os/os.h"
#include "core/object/callable_mp.h"
#include "editor/editor_node.h"
#include "editor/export/editor_export.h"
#include "editor/export/editor_export_platform.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "scene/gui/dialogs.h"

void ECSSkeletonEditorPlugin::open_workspace() {
	if(workspace) { return; }
	workspace=memnew(ECSAnimationEditor);
	EditorNode::get_singleton()->mount_tool_workspace(workspace);
	auto window=get_tree()->get_root();
	window->set_title(String(U"AgeSkeleton — 2D 骨骼动画编辑器"));
	window->set_min_size(Size2i(1000,650));
	close_dialog=memnew(ConfirmationDialog); workspace->add_child(close_dialog);
	close_dialog->set_title(String(U"退出骨骼编辑器")); close_dialog->set_text(String(U"请确认已保存需要保留的骨骼工程。退出独立编辑器？"));
	close_dialog->get_ok_button()->set_text(String(U"退出"));
	close_dialog->connect("confirmed",callable_mp(get_tree(),&SceneTree::quit).bind(0));
	EditorNode::get_singleton()->connect("tool_workspace_close_requested",callable_mp(this,&ECSSkeletonEditorPlugin::request_close));
	Ref<ECSScene> scene;
	String path=GLOBAL_GET("ecs/run/scene");
	if(!path.is_empty()) { scene=ResourceLoader::load(path,"ECSScene"); }
	workspace->start_independent_project(scene);
	const List<String> arguments=OS::get_singleton()->get_cmdline_user_args();
	for(auto *arg=arguments.front();arg;arg=arg->next()) { if(arg->get()=="--open-document" && arg->next()) { workspace->open_document(arg->next()->get()); break; } }
	print_line("AGE_SKELETON_READY");
	if(OS::get_singleton()->get_cmdline_user_args().find("--skeleton-self-test")) {
		get_tree()->create_timer(1)->connect("timeout",callable_mp(this,&ECSSkeletonEditorPlugin::self_test));
	}
}
void ECSSkeletonEditorPlugin::request_close() { close_dialog->popup_centered(); }
bool skeleton_image_codec_self_test();
bool skeleton_runtime_atlas_self_test();
void ECSSkeletonEditorPlugin::self_test() {
	bool ok=skeleton_image_codec_self_test();
    ok &= SkeletonMeshContour::self_test();
	ok &= ECSWorld::skeleton_slots_self_test();
	ok &= ECSWorld::skeleton_animation_channels_self_test();
	ok &= workspace->run_self_test();
	ok &= skeleton_runtime_atlas_self_test();
	ok &= workspace->prepare_platform_export();
	bool windows=false,linux=false,mac=false,web=false;
	for(int i=0;i<EditorExport::get_singleton()->get_export_platform_count();i++) {
		String name=EditorExport::get_singleton()->get_export_platform(i)->get_name();
		print_line("SKELETON_EXPORT_PLATFORM "+name);
		windows |= name=="Windows Desktop"; linux |= name=="Linux"; mac |= name=="macOS"; web |= name=="Web";
	}
	ok &= windows && linux && mac && web;
	print_line(ok?"AGE_SKELETON_STANDALONE_PASS":"AGE_SKELETON_STANDALONE_FAIL");
	get_tree()->quit(ok?0:1);
}
void ECSSkeletonEditorPlugin::_notification(int what) {
	if(what==NOTIFICATION_ENTER_TREE) {
		auto *editor=EditorNode::get_singleton();
		if(editor->is_editor_layout_loaded()) { callable_mp(this,&ECSSkeletonEditorPlugin::open_workspace).call_deferred(); }
		else { editor->connect("editor_layout_loaded",callable_mp(this,&ECSSkeletonEditorPlugin::open_workspace),CONNECT_ONE_SHOT); }
	}
	if(what==NOTIFICATION_EXIT_TREE && workspace) {
		workspace->stop_preview(); workspace->get_parent()->remove_child(workspace); memdelete(workspace); workspace=nullptr;
	}
}
#endif
