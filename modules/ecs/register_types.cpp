#include "register_types.h"

#include "ecs_main_loop.h"
#include "ecs_scene.h"
#include "ecs_world.h"
#include "ecs_replication.h"

#include "core/config/project_settings.h"
#include "core/object/class_db.h"
#ifdef TOOLS_ENABLED
#include "ecs_editor.h"
#include "ecs_workspace_docking.h"
#include "ecs_sprite_atlas_editor.h"

#include "editor/editor_node.h"
#endif
void initialize_ecs_module(ModuleInitializationLevel level) {
	if (level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(ECSWorld);
		GDREGISTER_CLASS(ECSReplication);
		GDREGISTER_CLASS(ECSScene);
		GDREGISTER_CLASS(ECSMainLoop);
		GLOBAL_DEF(PropertyInfo(Variant::STRING, "ecs/run/scene", PROPERTY_HINT_FILE, "*.json,*.tres,*.res"), "");
		GLOBAL_DEF("ecs/run/quit_after_seconds", 0.0);
		GLOBAL_DEF("ecs/run/screenshot", "");
		GLOBAL_DEF("ecs/run/self_test", false);
		GLOBAL_DEF("ecs/run/benchmark", false);
		GLOBAL_DEF("ecs/game/assembly", "");
		GLOBAL_DEF("ecs/game/class", "");
#ifdef TOOLS_ENABLED
		GLOBAL_DEF("ecs/editor/ai_tools", false);
#endif
	}
#ifdef TOOLS_ENABLED
	if (level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		GDREGISTER_CLASS(SpriteAtlas);
		GDREGISTER_INTERNAL_CLASS(SpriteAtlasPanel);
		GDREGISTER_INTERNAL_CLASS(SpriteAtlasExportPlugin);
		GDREGISTER_INTERNAL_CLASS(SpriteAtlasEditorPlugin);
		EditorPlugins::add_by_type<SpriteAtlasEditorPlugin>();
		GDREGISTER_INTERNAL_CLASS(ECSDebuggerPanel);
		GDREGISTER_INTERNAL_CLASS(ECSDebuggerPlugin);
		GDREGISTER_INTERNAL_CLASS(ECSEditorPlugin);
		GDREGISTER_INTERNAL_CLASS(ECSSceneEntityEditor);
		GDREGISTER_INTERNAL_CLASS(ECSSceneEditorPanel);
		GDREGISTER_INTERNAL_CLASS(ECSUICanvasEditor);
		GDREGISTER_INTERNAL_CLASS(ECSSpatialEditor);
		GDREGISTER_INTERNAL_CLASS(ECSWorkspaceDocking);
		initialize_ecs_editor();
	}
#endif
}
void uninitialize_ecs_module(ModuleInitializationLevel level) {
#ifdef TOOLS_ENABLED
	if (level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		uninitialize_ecs_editor();
	}
#endif
}

#ifdef TOOLS_ENABLED
bool is_age_skeleton_editor_build() {
#ifdef AGE_SKELETON_EDITOR
    return true;
#else
    return false;
#endif
}
#endif
