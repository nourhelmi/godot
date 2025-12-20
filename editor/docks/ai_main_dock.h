/**************************************************************************/
/*  ai_main_dock.h                                                        */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GAMEABLE                                   */
/*                     AI-powered game development                        */
/**************************************************************************/

#pragma once

#include "scene/gui/panel_container.h"

class AIChatDock;
class AIContextDock; // kept for bundle/settings UI later
class AIHarnessDock;
class Button;
class HBoxContainer;
class Label;
class RichTextLabel;
class StyleBoxFlat;
class TabContainer;
class VBoxContainer;

// Main AI dock container with TabContainer (Chat/Context/Harness),
// connection status bar, and integrated bottom logs panel.
class AIMainDock : public PanelContainer {
	GDCLASS(AIMainDock, PanelContainer);

	// Theme cache
	struct ThemeCache {
		Ref<StyleBoxFlat> status_connected;
		Ref<StyleBoxFlat> status_disconnected;
		Ref<StyleBoxFlat> status_connecting;
	} theme_cache;

	// Main layout
	VBoxContainer *root = nullptr;
	TabContainer *tabs = nullptr;

	// Child docks
	AIChatDock *chat_dock = nullptr;
	AIContextDock *context_dock = nullptr;
	AIHarnessDock *harness_dock = nullptr;

	// Status bar
	HBoxContainer *status_bar = nullptr;
	Label *status_indicator = nullptr;
	Label *status_label = nullptr;
	Button *reconnect_btn = nullptr;

	// Bottom logs (set externally by plugin)
	RichTextLabel *bottom_logs = nullptr;

	void _build_ui();
	void _build_child_docks();
	void _build_styles();
	void _build_status_bar();

	// Agent signal handlers
	void _on_connection_state_changed(int p_state);
	void _on_verify_result(const Dictionary &p_result);

	void _on_reconnect_pressed();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_bottom_logs(RichTextLabel *p_logs);
	void log_message(const String &p_msg);

	// Access to child docks
	AIChatDock *get_chat_dock() const { return chat_dock; }
	AIContextDock *get_context_dock() const { return context_dock; }
	AIHarnessDock *get_harness_dock() const { return harness_dock; }

	AIMainDock();
	~AIMainDock();
};
