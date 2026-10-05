#include "register_types.h"
#include "ecs_skeleton_editor_plugin.h"
#include "core/object/class_db.h"
#include "editor/editor_node.h"

void initialize_age_skeleton_module(ModuleInitializationLevel level) {
    if (level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
        GDREGISTER_INTERNAL_CLASS(ECSSkeletonEditorPlugin);
        EditorPlugins::add_by_type<ECSSkeletonEditorPlugin>();
    }
}
void uninitialize_age_skeleton_module(ModuleInitializationLevel level) {
}
