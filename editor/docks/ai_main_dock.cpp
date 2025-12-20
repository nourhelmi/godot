/**************************************************************************/
/*  ai_main_dock.cpp                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GAMEABLE                                   */
/*                     AI-powered game development                        */
/**************************************************************************/

#include "ai_main_dock.h"

#include "ai_chat_dock.h"
#include "ai_context_dock.h"
#include "ai_harness_dock.h"
#include "core/string/print_string.h"
#include "editor/ai/editor_ai_agent.h"
#include "editor/editor_node.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/rich_text_label.h"
#include "scene/gui/tab_container.h"
#include "scene/resources/style_box_flat.h"

void AIMainDock::_bind_methods() {
	// No exposed methods yet
}

void AIMainDock::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			// Create child docks here (deferred from constructor) to avoid
			// "parent busy" errors during editor initialization
			_build_child_docks();
			_build_styles();

			// Connect to AI agent and start connection
			if (EditorAIAgent *agent = EditorAIAgent::get_singleton()) {
				agent->connect("connection_state_changed", callable_mp(this, &AIMainDock::_on_connection_state_changed));
				agent->connect("verify_result", callable_mp(this, &AIMainDock::_on_verify_result));
				agent->connect_to_server();
			}
		} break;

		case NOTIFICATION_THEME_CHANGED: {
			_build_styles();
		} break;
	}
}

AIMainDock::AIMainDock() {
	set_name("Gameable");
	_build_ui();
}

AIMainDock::~AIMainDock() {
}

void AIMainDock::_build_ui() {
	// Build basic container structure - can be called from constructor
	root = memnew(VBoxContainer);
	add_child(root);

	// Tab container (child docks added later in _build_child_docks)
	tabs = memnew(TabContainer);
	tabs->set_v_size_flags(SIZE_EXPAND_FILL);
	tabs->set_tab_alignment(TabBar::ALIGNMENT_CENTER);
	root->add_child(tabs);

	// Status bar at bottom
	_build_status_bar();
}

void AIMainDock::_build_child_docks() {
	// Create child docks - called from NOTIFICATION_READY to avoid
	// "parent busy" errors when each dock builds its own UI
	chat_dock = memnew(AIChatDock);
	tabs->add_child(chat_dock);

	// Context tab removed - context now managed via @ mentions + right-click menus
	// context_dock kept as member for potential settings/bundle UI later
	context_dock = nullptr;

	harness_dock = memnew(AIHarnessDock);
	tabs->add_child(harness_dock);
}

void AIMainDock::_build_styles() {
	Ref<Theme> theme = EditorNode::get_singleton() ? EditorNode::get_singleton()->get_editor_theme() : nullptr;
	if (!theme.is_valid()) {
		return;
	}

	Color base_color = theme->get_color("base_color", "Editor");
	float radius = 8 * EDSCALE;

	// Connected: green dot
	theme_cache.status_connected.instantiate();
	theme_cache.status_connected->set_bg_color(Color(0.3, 0.8, 0.4));
	theme_cache.status_connected->set_corner_radius_all(radius);

	// Disconnected: gray dot
	theme_cache.status_disconnected.instantiate();
	theme_cache.status_disconnected->set_bg_color(base_color.lightened(0.3));
	theme_cache.status_disconnected->set_corner_radius_all(radius);

	// Connecting: orange dot
	theme_cache.status_connecting.instantiate();
	theme_cache.status_connecting->set_bg_color(Color(0.9, 0.6, 0.2));
	theme_cache.status_connecting->set_corner_radius_all(radius);
}

void AIMainDock::_build_status_bar() {
	status_bar = memnew(HBoxContainer);
	status_bar->add_theme_constant_override("separation", 8 * EDSCALE);
	root->add_child(status_bar);

	// Status indicator dot
	status_indicator = memnew(Label);
	status_indicator->set_text(U"●");
	status_indicator->add_theme_font_size_override("font_size", 10 * EDSCALE);
	status_indicator->add_theme_color_override("font_color", Color(0.5, 0.5, 0.5));
	status_bar->add_child(status_indicator);

	// Status text
	status_label = memnew(Label);
	status_label->set_text("Disconnected");
	status_label->set_h_size_flags(SIZE_EXPAND_FILL);
	status_label->add_theme_font_size_override("font_size", 11 * EDSCALE);
	status_bar->add_child(status_label);

	// Reconnect button
	reconnect_btn = memnew(Button);
	reconnect_btn->set_text("Reconnect");
	reconnect_btn->set_flat(true);
	reconnect_btn->add_theme_font_size_override("font_size", 11 * EDSCALE);
	status_bar->add_child(reconnect_btn);
	reconnect_btn->connect("pressed", callable_mp(this, &AIMainDock::_on_reconnect_pressed));
}

void AIMainDock::_on_connection_state_changed(int p_state) {
	switch (p_state) {
		case AI_CONNECTION_DISCONNECTED:
			status_label->set_text("Disconnected");
			status_indicator->add_theme_color_override("font_color", Color(0.5, 0.5, 0.5));
			reconnect_btn->set_visible(true);
			break;
		case AI_CONNECTION_CONNECTING:
			status_label->set_text("Connecting...");
			status_indicator->add_theme_color_override("font_color", Color(0.9, 0.6, 0.2));
			reconnect_btn->set_visible(false);
			break;
		case AI_CONNECTION_CONNECTED:
			status_label->set_text("Connected");
			status_indicator->add_theme_color_override("font_color", Color(0.3, 0.8, 0.4));
			reconnect_btn->set_visible(false);
			log_message("Connected to agent");
			break;
		case AI_CONNECTION_CLOSING:
			status_label->set_text("Closing...");
			status_indicator->add_theme_color_override("font_color", Color(0.9, 0.6, 0.2));
			reconnect_btn->set_visible(false);
			break;
	}
}

void AIMainDock::_on_verify_result(const Dictionary &p_result) {
	// Switch to harness tab when verification completes (index 1 after Context removed)
	tabs->set_current_tab(1);
}

void AIMainDock::_on_reconnect_pressed() {
	if (EditorAIAgent *agent = EditorAIAgent::get_singleton()) {
		agent->connect_to_server();
	}
}

void AIMainDock::set_bottom_logs(RichTextLabel *p_logs) {
	bottom_logs = p_logs;
}

void AIMainDock::log_message(const String &p_msg) {
	print_line("[Gameable] ", p_msg);
	if (bottom_logs) {
		bottom_logs->append_text(p_msg + "\n");
	}
}
