/**************************************************************************/
/*  ai_chat_dock.h                                                        */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GAMEABLE                                   */
/*                     AI-powered game development                        */
/**************************************************************************/

#pragma once

#include "scene/gui/box_container.h"

class Button;
class HBoxContainer;
class InputEvent;
class Label;
class LineEdit;
class PanelContainer;
class RichTextLabel;
class ScrollContainer;
class StyleBoxFlat;
class TextEdit;

// Beautiful chat panel with soft message bubbles, elegant token meter,
// collapsible reasoning pane, and animated tool progress cards.
class AIChatDock : public VBoxContainer {
	GDCLASS(AIChatDock, VBoxContainer);

	// Theme cache for styled components
	struct ThemeCache {
		Ref<StyleBoxFlat> message_bg_user;
		Ref<StyleBoxFlat> message_bg_assistant;
		Ref<StyleBoxFlat> reasoning_bg;
		Ref<StyleBoxFlat> tool_card_bg;
		Ref<StyleBoxFlat> input_bg;
		Ref<StyleBoxFlat> meter_bg;
		Color accent_color;
		Color text_muted;
		int corner_radius = 6;
	} theme_cache;

	// Header: token meter + new conversation button
	HBoxContainer *header = nullptr;
	PanelContainer *meter_container = nullptr;
	Label *token_meter = nullptr;
	Button *new_btn = nullptr;

	// Tool progress area (floating cards above chat)
	VBoxContainer *tool_progress_area = nullptr;

	// Main chat scroll area
	ScrollContainer *scroll = nullptr;
	VBoxContainer *messages_container = nullptr;

	// Collapsible reasoning section
	VBoxContainer *reasoning_section = nullptr;
	Button *reasoning_toggle = nullptr;
	PanelContainer *reasoning_panel = nullptr;
	RichTextLabel *reasoning_text = nullptr;
	bool reasoning_collapsed = true;
	String current_reasoning;

	// Input area
	PanelContainer *input_container = nullptr;
	HBoxContainer *input_row = nullptr;
	TextEdit *input = nullptr;
	Button *send_btn = nullptr;

	// State tracking
	int64_t turn_tokens = 0;
	int64_t session_tokens = 0;
	int tool_call_count = 0;

	// Active tool progress cards
	HashMap<String, PanelContainer *> active_tool_cards;
	HashMap<String, uint64_t> tool_done_times;

	// Internal UI builders
	void _build_ui();
	void _build_styles();
	void _build_header();
	void _build_tool_progress_area();
	void _build_messages_area();
	void _build_reasoning_section();
	void _build_input_area();

	// Message bubble creation
	PanelContainer *_create_message_bubble(const String &p_role, const String &p_text);

	// Event handlers
	void _on_send_pressed();
	void _on_input_gui_input(const Ref<InputEvent> &p_event);
	void _on_new_conversation();
	void _on_reasoning_toggle();
	void _on_meta_clicked(const Variant &p_meta);

	// Agent signal handlers
	void _on_thinking(const String &p_text);
	void _on_status(const String &p_level, const String &p_message);
	void _on_usage_updated(int64_t p_turn, int64_t p_session);
	void _on_tool_call(const String &p_id, const String &p_name, const Dictionary &p_input);
	void _on_tool_result(const String &p_id, bool p_ok, const Dictionary &p_output);
	void _on_tool_progress(const String &p_id, const String &p_stage, float p_progress);
	void _on_chat_message(const String &p_role, const String &p_text);

	void _update_meter();
	void _scroll_to_bottom();
	void _do_scroll_to_bottom();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	// Public API for message display
	void append_message(const String &p_role, const String &p_text);
	void append_assistant_chunk(const String &p_text);
	void append_thinking(const String &p_text);
	void update_usage(int64_t p_turn, int64_t p_session);
	void add_tool_progress(const String &p_id, const String &p_name, const String &p_stage);
	void mark_tool_done(const String &p_id);
	void cleanup_done_tools();
	void reset_turn_state();
	void clear_all();

	AIChatDock();
	~AIChatDock();
};
