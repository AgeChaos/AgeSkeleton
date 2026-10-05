#pragma once
#ifdef TOOLS_ENABLED
#include "editor/plugins/editor_plugin.h"
class ECSAnimationEditor;
class ConfirmationDialog;
class ECSSkeletonEditorPlugin : public EditorPlugin {
	GDCLASS(ECSSkeletonEditorPlugin, EditorPlugin);
	ECSAnimationEditor *workspace = nullptr;
	ConfirmationDialog *close_dialog=nullptr;
	void request_close();
	void open_workspace();
	void self_test();
protected:
	static void _bind_methods() {}
	void _notification(int p_what);
public:
	String get_plugin_name() const override { return "AgeSkeleton"; }
};
#endif
