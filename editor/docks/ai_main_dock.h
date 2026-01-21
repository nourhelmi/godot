/**************************************************************************/
/*  ai_main_dock.h                                                        */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GAMEABLE                                   */
/*                     AI-powered game development                        */
/**************************************************************************/

#pragma once

#include "editor/docks/editor_dock.h"

class AIChatDock;
class AIContextDock; // kept for bundle/settings UI later
class AIRuntimeDock;
class Button;
class HBoxContainer;
class Label;
class RichTextLabel;
class StyleBoxFlat;
class TabContainer;
class VBoxContainer;

// Main AI dock container with TabContainer (Chat/Live),
// connection status bar, and integrated bottom logs panel.
class AIMainDock : public EditorDock {
	GDCLASS(AIMainDock, EditorDock);

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
	AIRuntimeDock *runtime_dock = nullptr;

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
	void _on_runtime_state_changed(const String &p_state, const String &p_scene);

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
	AIRuntimeDock *get_runtime_dock() const { return runtime_dock; }

	AIMainDock();
	~AIMainDock();
};
