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
class Control;
class FlowContainer;
class HBoxContainer;
class Image;
class InputEvent;
class Label;
class PanelContainer;
class ProgressBar;
class RichTextLabel;
class ScrollContainer;
class StyleBoxFlat;
class TextEdit;
class Texture2D;
class TextureRect;

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
	Button *mode_btn = nullptr;
	Button *new_btn = nullptr;

	// Main chat scroll area
	ScrollContainer *scroll = nullptr;
	VBoxContainer *messages_container = nullptr;

	// Context chips above input (for @ mentioned items)
	FlowContainer *pinned_chips = nullptr;
	FlowContainer *context_chips = nullptr;
	FlowContainer *attachment_chips = nullptr;
	Vector<String> mentioned_paths; // paths mentioned via @ in current input

	// Input area
	PanelContainer *input_container = nullptr;
	HBoxContainer *input_row = nullptr;
	TextEdit *input = nullptr;
	Button *capture_btn = nullptr;
	Button *send_btn = nullptr;
	Button *stop_btn = nullptr;

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
	String current_response_buffer; // Accumulate raw markdown for re-rendering
	VBoxContainer *current_tools_container = nullptr;
	bool has_thinking = false;
	bool has_response = false;

	// State tracking
	int64_t turn_tokens = 0;
	int64_t session_tokens = 0;
	int cache_rate = 0; // 0-100 percentage of prompt tokens from cache
	int tool_call_count = 0;
	bool stick_to_bottom = true; // Only auto-scroll when the user is already at the bottom.
	bool plan_mode_enabled = false;

	// Active tool cards within current turn
	HashMap<String, PanelContainer *> active_tool_cards;
	HashMap<String, Label *> active_tool_icons;
	HashMap<String, Label *> active_tool_labels;
	HashMap<String, RichTextLabel *> active_tool_details;
	HashMap<String, Control *> active_tool_detail_containers;
	HashMap<String, Button *> active_tool_detail_toggles;
	HashMap<String, ProgressBar *> active_tool_progress_bars;
	HashMap<String, TextureRect *> active_tool_previews;
	HashMap<String, Dictionary> active_tool_inputs;
	HashMap<String, String> active_tool_names;
	HashMap<String, uint64_t> tool_done_times;
	uint64_t last_spinner_msec = 0;
	int spinner_frame = 0;

	struct PendingImage {
		String id;
		String label;
		String mime;
		String data_base64;
		Ref<Texture2D> preview;
	};
	Vector<PendingImage> pending_images;
	uint64_t image_sequence = 0;

	// Internal UI builders
	void _build_ui();
	void _build_styles();
	void _build_header();
	void _build_messages_area();
	void _build_input_area();
	void _build_attachment_chips_area();

	// Turn management - creates assistant turn container for streaming
	void _ensure_assistant_turn();
	void _ensure_thinking_block();
	void _ensure_response_block();
	void _ensure_tools_container();

	// Message bubble creation
	PanelContainer *_create_user_bubble(const String &p_text);
	PanelContainer *_create_tool_card(const String &p_id, const String &p_name, const String &p_status);
	String _format_tool_status(const String &p_name, const Dictionary &p_input) const;
	String _format_tool_progress(const String &p_name, const String &p_stage, const Dictionary &p_input) const;
	String _format_tool_summary(const String &p_name, bool p_ok, const Dictionary &p_output, const Dictionary &p_input) const;
	String _format_tool_details(const String &p_name, bool p_ok, const Dictionary &p_output, const Dictionary &p_input) const;
	String _extract_generated_image_path(const Dictionary &p_output) const;
	Ref<Texture2D> _load_generated_image_preview(const String &p_path) const;
	void _attach_tool_image_preview(const String &p_tool_id, const String &p_image_path);
	void _on_tool_details_toggle(Control *p_container, Button *p_toggle);
	void _update_spinner_icons();

	// Event handlers
	void _on_send_pressed();
	void _on_stop_pressed();
	void _on_input_gui_input(const Ref<InputEvent> &p_event);
	void _on_input_text_changed();
	void _on_mode_toggled();
	void _on_new_conversation();
	void _on_meta_clicked(const Variant &p_meta);
	void _on_processing_state_changed(bool p_is_processing);
	void _refresh_mode_button();

	// @ mention handler
	void _on_mention_selected(const String &p_path, const String &p_label, int p_start_col);

	// Context chip helpers
	void _build_pinned_chips_area();
	void _build_context_chips_area();
	void _add_image_chip(const PendingImage &p_image);
	void _add_context_chip(const String &p_path, const String &p_label);
	void _remove_context_chip(const String &p_path);
	void _clear_context_chips();
	void _remove_image_attachment(const String &p_id);
	void _clear_image_attachments();
	void _add_pinned_chip(const String &p_item_id, const String &p_label);
	void _clear_pinned_chips();
	void _remove_pinned_chip(const String &p_item_id);
	void _refresh_pinned_chips(const Array &p_items);

	// Thinking block collapse
	void _on_thinking_toggle();
	void _on_capture_pressed();
	bool _try_attach_clipboard_image();
	void _add_image_attachment(const Ref<Image> &p_image, const String &p_label);
	Ref<Image> _capture_viewport_image(String &r_label) const;

	// Agent signal handlers
	void _on_thinking(const String &p_text);
	void _on_status(const String &p_level, const String &p_message);
	void _on_usage_updated(int64_t p_turn, int64_t p_session, int p_cache_rate);
	void _on_tool_call(const String &p_id, const String &p_name, const Dictionary &p_input);
	void _on_tool_result(const String &p_id, bool p_ok, const Dictionary &p_output, bool p_preliminary);
	void _on_tool_progress(const String &p_id, const String &p_stage, float p_progress);
	void _on_chat_message(const String &p_role, const String &p_text);
	void _on_context_updated(const Array &p_items);

	// Scroll state
	void _on_scroll_value_changed(double p_value);

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
	void add_tool_card(const String &p_id, const String &p_name, const Dictionary &p_input, const String &p_status);
	void update_tool_card(const String &p_id, const String &p_status, bool p_done, bool p_success = true);
	void update_usage(int64_t p_turn, int64_t p_session, int p_cache_rate);
	void cleanup_done_tools();
	void end_turn(); // Finalizes current turn, clears streaming state
	void clear_all();

	AIChatDock();
	~AIChatDock();
};
