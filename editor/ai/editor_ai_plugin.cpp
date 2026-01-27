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
#include "editor/docks/editor_dock.h"
#include "editor/docks/editor_dock_manager.h"
#include "editor/editor_node.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/inspector/editor_context_menu_plugin.h"
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

			// Create main dock (tab index set via set_default_tab_index in constructor)
			main_dock = memnew(AIMainDock);
			add_dock(main_dock);
			EditorDockManager::get_singleton()->focus_dock(main_dock);

			// Ensure Gameable sits first after layout load completes.
			callable_mp(this, &EditorAIPlugin::_ensure_gameable_dock_first).call_deferred();
			if (EditorFileSystem *fs = EditorFileSystem::get_singleton()) {
				const Callable on_sources_changed = callable_mp(this, &EditorAIPlugin::_on_sources_changed);
				if (!fs->is_connected("sources_changed", on_sources_changed)) {
					fs->connect("sources_changed", on_sources_changed);
				}
			}

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
			if (EditorFileSystem *fs = EditorFileSystem::get_singleton()) {
				const Callable on_sources_changed = callable_mp(this, &EditorAIPlugin::_on_sources_changed);
				if (fs->is_connected("sources_changed", on_sources_changed)) {
					fs->disconnect("sources_changed", on_sources_changed);
				}
			}

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
				remove_dock(main_dock);
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

void EditorAIPlugin::_ensure_gameable_dock_first() {
	if (!main_dock || !main_dock->is_inside_tree()) {
		return;
	}
	_move_dock_to_front(main_dock);

	TabContainer *tab_container = Object::cast_to<TabContainer>(main_dock->get_parent());
	if (!tab_container || tab_container->get_tab_count() == 0) {
		return;
	}

	const int target_idx = tab_container->get_tab_idx_from_control(main_dock);
	if (target_idx >= 0 && tab_container->get_current_tab() != target_idx) {
		tab_container->set_current_tab(target_idx);
	}
}

void EditorAIPlugin::_move_dock_to_front(EditorDock *p_dock) {
	ERR_FAIL_NULL(p_dock);
	if (!p_dock->is_inside_tree()) {
		return;
	}

	TabContainer *tab_container = Object::cast_to<TabContainer>(p_dock->get_parent());
	if (!tab_container || tab_container->get_tab_count() == 0) {
		return;
	}

	const int current_idx = tab_container->get_tab_idx_from_control(p_dock);
	if (current_idx <= 0) {
		return;
	}

	Control *front_tab = tab_container->get_tab_control(0);
	if (!front_tab) {
		return;
	}
	tab_container->move_child(p_dock, front_tab->get_index(false));
}

void EditorAIPlugin::_on_sources_changed(bool p_exist) {
	(void)p_exist;
	_ensure_gameable_dock_first();
	if (EditorFileSystem *fs = EditorFileSystem::get_singleton()) {
		const Callable on_sources_changed = callable_mp(this, &EditorAIPlugin::_on_sources_changed);
		if (fs->is_connected("sources_changed", on_sources_changed)) {
			fs->disconnect("sources_changed", on_sources_changed);
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
