/**************************************************************************/
/*  editor_ai_plugin.h                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GAMEABLE                                   */
/*                     AI-powered game development                        */
/**************************************************************************/

#pragma once

#include "editor/plugins/editor_plugin.h"

class AIMainDock;
class Button;
class EditorAIMenuHandler;
class EditorDock;
class RichTextLabel;

// Editor plugin that registers the AI dock and bottom panel.
// This is the main entry point for Gameable AI integration.
class EditorAIPlugin : public EditorPlugin {
	GDCLASS(EditorAIPlugin, EditorPlugin);

	AIMainDock *main_dock = nullptr;
	RichTextLabel *bottom_logs = nullptr;
	Button *bottom_toggle_btn = nullptr;

	// Context menu handlers (must stay alive)
	Ref<EditorAIMenuHandler> filesystem_menu_handler;
	Ref<EditorAIMenuHandler> scene_tree_menu_handler;

	void _ensure_gameable_dock_first();
	void _move_dock_to_front(EditorDock *p_dock);
	void _on_sources_changed(bool p_exist);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	EditorAIPlugin();
	~EditorAIPlugin();
};

// Initialization functions called from register_editor_types
void initialize_gameable_ai();
void uninitialize_gameable_ai();
