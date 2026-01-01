/**************************************************************************/
/*  ai_chat_dock.h                                                        */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GAMEABLE                                   */
/*                     AI-powered game development                        */
/**************************************************************************/

#pragma once

#include "scene/gui/box_container.h"

class AIMentionPopup;
class Button;
class FlowContainer;
class HBoxContainer;
class InputEvent;
class Label;
class PanelContainer;
class RichTextLabel;
class ScrollContainer;
class StyleBoxFlat;
class TextEdit;

// Chat panel with inline streaming: reasoning → response → tools flow sequentially
// like Cursor/Claude. Each assistant turn is a container holding all parts.
class AIChatDock : public VBoxContainer {
	GDCLASS(AIChatDock, VBoxContainer);

	// Theme cache
	struct ThemeCache {
		Ref<StyleBoxFlat> message_bg_user;
		Ref<StyleBoxFlat> message_bg_assistant;
		Ref<StyleBoxFlat> thinking_bg; // Subtle style for reasoning blocks
		Ref<StyleBoxFlat> tool_card_bg;
		Ref<StyleBoxFlat> input_bg;
		Ref<StyleBoxFlat> meter_bg;
		Color accent_color;
		Color text_muted;
		Color thinking_color; // Dimmed color for reasoning text
		int corner_radius = 6;
	} theme_cache;

	// Header: token meter + new conversation button
	HBoxContainer *header = nullptr;
	PanelContainer *meter_container = nullptr;
	Label *token_meter = nullptr;
	Button *new_btn = nullptr;

	// Main chat scroll area
	ScrollContainer *scroll = nullptr;
	VBoxContainer *messages_container = nullptr;

	// Context chips above input (for @ mentioned items)
	FlowContainer *pinned_chips = nullptr;
	FlowContainer *context_chips = nullptr;
	Vector<String> mentioned_paths; // paths mentioned via @ in current input

	// Input area
	PanelContainer *input_container = nullptr;
	HBoxContainer *input_row = nullptr;
	TextEdit *input = nullptr;
	Button *send_btn = nullptr;

	// @ mention autocomplete
	AIMentionPopup *mention_popup = nullptr;

	// Current assistant turn container (for streaming)
	// Structure: [thinking_container] [response_text] [tool_cards...]
	VBoxContainer *current_turn = nullptr;
	PanelContainer *current_thinking_block = nullptr;
	Button *current_thinking_toggle = nullptr; // toggle collapse
	RichTextLabel *current_thinking_text = nullptr;
	bool thinking_collapsed = false;
	RichTextLabel *current_response_text = nullptr;
	VBoxContainer *current_tools_container = nullptr;
	bool has_thinking = false;
	bool has_response = false;

	// State tracking
	int64_t turn_tokens = 0;
	int64_t session_tokens = 0;
	int tool_call_count = 0;

	// Active tool cards within current turn
	HashMap<String, PanelContainer *> active_tool_cards;
	HashMap<String, uint64_t> tool_done_times;

	// Internal UI builders
	void _build_ui();
	void _build_styles();
	void _build_header();
	void _build_messages_area();
	void _build_input_area();

	// Turn management - creates assistant turn container for streaming
	void _ensure_assistant_turn();
	void _ensure_thinking_block();
	void _ensure_response_block();
	void _ensure_tools_container();

	// Message bubble creation
	PanelContainer *_create_user_bubble(const String &p_text);
	PanelContainer *_create_tool_card(const String &p_name, const String &p_status);

	// Event handlers
	void _on_send_pressed();
	void _on_input_gui_input(const Ref<InputEvent> &p_event);
	void _on_input_text_changed();
	void _on_new_conversation();
	void _on_meta_clicked(const Variant &p_meta);

	// @ mention handler
	void _on_mention_selected(const String &p_path, const String &p_label, int p_start_col);

	// Context chip helpers
	void _build_pinned_chips_area();
	void _build_context_chips_area();
	void _add_context_chip(const String &p_path, const String &p_label);
	void _remove_context_chip(const String &p_path);
	void _clear_context_chips();
	void _add_pinned_chip(const String &p_item_id, const String &p_label);
	void _clear_pinned_chips();
	void _remove_pinned_chip(const String &p_item_id);
	void _refresh_pinned_chips(const Array &p_items);

	// Thinking block collapse
	void _on_thinking_toggle();

	// Agent signal handlers
	void _on_thinking(const String &p_text);
	void _on_status(const String &p_level, const String &p_message);
	void _on_usage_updated(int64_t p_turn, int64_t p_session);
	void _on_tool_call(const String &p_id, const String &p_name, const Dictionary &p_input);
	void _on_tool_result(const String &p_id, bool p_ok, const Dictionary &p_output);
	void _on_tool_progress(const String &p_id, const String &p_stage, float p_progress);
	void _on_chat_message(const String &p_role, const String &p_text);
	void _on_context_updated(const Array &p_items);

	void _update_meter();
	void _scroll_to_bottom();
	void _do_scroll_to_bottom();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	// Public API - streaming content into current turn
	void append_user_message(const String &p_text);
	void append_thinking(const String &p_text);
	void append_response(const String &p_text);
	void add_tool_card(const String &p_id, const String &p_name, const String &p_status);
	void update_tool_card(const String &p_id, const String &p_status, bool p_done);
	void update_usage(int64_t p_turn, int64_t p_session);
	void cleanup_done_tools();
	void end_turn(); // Finalizes current turn, clears streaming state
	void clear_all();

	AIChatDock();
	~AIChatDock();
};
