/**************************************************************************/
/*  editor_ai_plugin.cpp                                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GAMEABLE                                   */
/*                     AI-powered game development                        */
/**************************************************************************/

#include "editor_ai_plugin.h"

#include "editor/ai/editor_ai_agent.h"
#include "editor/ai/editor_ai_menu_handler.h"
#include "editor/docks/ai_main_dock.h"
#include "editor/inspector/editor_context_menu_plugin.h"
#include "editor/docks/editor_dock_manager.h"
#include "editor/editor_node.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/rich_text_label.h"
#include "scene/gui/tab_container.h"

void EditorAIPlugin::_bind_methods() {
	// No exposed methods
}

void EditorAIPlugin::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			// Define editor settings
			EDITOR_DEF("gameable/enable", true);
			EDITOR_DEF("gameable/ws_url", "ws://127.0.0.1:1999/session/dev");

			if (!bool(EDITOR_GET("gameable/enable"))) {
				return;
			}

			// Create main dock
			main_dock = memnew(AIMainDock);
			add_control_to_dock(DOCK_SLOT_RIGHT_UL, main_dock);
			_ensure_dock_first();
			EditorDockManager::get_singleton()->focus_dock(main_dock);

			// Create bottom panel logs
			bottom_logs = memnew(RichTextLabel);
			bottom_logs->set_autowrap_mode(TextServer::AUTOWRAP_WORD);
			bottom_logs->set_v_size_flags(Control::SIZE_EXPAND_FILL);
			bottom_logs->set_selection_enabled(true);

			// Use monospace font
			if (EditorNode::get_singleton()) {
				Ref<Theme> theme = EditorNode::get_singleton()->get_editor_theme();
				if (theme.is_valid()) {
					bottom_logs->add_theme_font_override("normal_font", theme->get_font("source", "EditorFonts"));
					bottom_logs->add_theme_font_size_override("normal_font_size", 12 * EDSCALE);
				}
			}

			bottom_toggle_btn = add_control_to_bottom_panel(bottom_logs, "Gameable");
			main_dock->set_bottom_logs(bottom_logs);

			// Register context menu handlers for "Add to AI Context"
			if (EditorContextMenuPluginManager *mgr = EditorContextMenuPluginManager::get_singleton()) {
				filesystem_menu_handler.instantiate();
				mgr->add_plugin(EditorContextMenuPlugin::CONTEXT_SLOT_FILESYSTEM, filesystem_menu_handler);

				scene_tree_menu_handler.instantiate();
				mgr->add_plugin(EditorContextMenuPlugin::CONTEXT_SLOT_SCENE_TREE, scene_tree_menu_handler);
			}
		} break;

		case NOTIFICATION_EXIT_TREE: {
			// Unregister context menu handlers
			if (EditorContextMenuPluginManager *mgr = EditorContextMenuPluginManager::get_singleton()) {
				if (filesystem_menu_handler.is_valid()) {
					mgr->remove_plugin(filesystem_menu_handler);
					filesystem_menu_handler.unref();
				}
				if (scene_tree_menu_handler.is_valid()) {
					mgr->remove_plugin(scene_tree_menu_handler);
					scene_tree_menu_handler.unref();
				}
			}

			if (main_dock) {
				remove_control_from_docks(main_dock);
				main_dock->queue_free();
				main_dock = nullptr;
			}
			if (bottom_logs) {
				remove_control_from_bottom_panel(bottom_logs);
				bottom_logs->queue_free();
				bottom_logs = nullptr;
			}
			bottom_toggle_btn = nullptr;
		} break;
	}
}

void EditorAIPlugin::_ensure_dock_first() {
	if (!main_dock) {
		return;
	}

	// Move dock to first position in its tab container
	Node *parent = main_dock->get_parent();
	while (parent && Object::cast_to<TabContainer>(parent) == nullptr) {
		parent = parent->get_parent();
	}
	if (TabContainer *tabs = Object::cast_to<TabContainer>(parent)) {
		if (tabs->get_child_count() > 0 && tabs->get_child(0) != main_dock) {
			tabs->move_child(main_dock, 0);
		}
	}
}

EditorAIPlugin::EditorAIPlugin() {
}

EditorAIPlugin::~EditorAIPlugin() {
}

// Called from register_editor_types.cpp
static void _editor_ai_init_callback() {
	EditorNode::get_singleton()->add_editor_plugin(memnew(EditorAIPlugin));
}

void initialize_gameable_ai() {
	// Create the AI agent singleton
	EditorAIAgent::create_singleton();

	// Register the editor plugin
	EditorNode::add_init_callback(_editor_ai_init_callback);
}

void uninitialize_gameable_ai() {
	EditorAIAgent::destroy_singleton();
}
