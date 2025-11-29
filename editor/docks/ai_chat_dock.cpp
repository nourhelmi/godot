/**************************************************************************/
/*  ai_chat_dock.cpp                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GAMEABLE                                   */
/*                     AI-powered game development                        */
/**************************************************************************/

#include "ai_chat_dock.h"

#include "core/input/input_event.h"
#include "core/io/resource_loader.h"
#include "core/os/os.h"
#include "editor/ai/editor_ai_agent.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/rich_text_label.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/text_edit.h"
#include "scene/resources/style_box_flat.h"

void AIChatDock::_bind_methods() {
	// No exposed methods yet
}

void AIChatDock::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			_build_styles();

			// Connect to AI agent signals
			if (EditorAIAgent *agent = EditorAIAgent::get_singleton()) {
				agent->connect("thinking", callable_mp(this, &AIChatDock::_on_thinking));
				agent->connect("status", callable_mp(this, &AIChatDock::_on_status));
				agent->connect("usage_updated", callable_mp(this, &AIChatDock::_on_usage_updated));
				agent->connect("tool_call", callable_mp(this, &AIChatDock::_on_tool_call));
				agent->connect("tool_result", callable_mp(this, &AIChatDock::_on_tool_result));
				agent->connect("tool_progress", callable_mp(this, &AIChatDock::_on_tool_progress));
				agent->connect("chat_message", callable_mp(this, &AIChatDock::_on_chat_message));
			}
		} break;

		case NOTIFICATION_THEME_CHANGED: {
			_build_styles();
		} break;

		case NOTIFICATION_PROCESS: {
			cleanup_done_tools();
		} break;
	}
}

AIChatDock::AIChatDock() {
	set_name("Chat");
	set_process(true);
	_build_ui();
}

AIChatDock::~AIChatDock() {
}

void AIChatDock::_build_ui() {
	_build_header();
	_build_messages_area();
	_build_input_area();
}

void AIChatDock::_build_styles() {
	Ref<Theme> theme = EditorNode::get_singleton() ? EditorNode::get_singleton()->get_editor_theme() : nullptr;
	if (!theme.is_valid()) {
		return;
	}

	theme_cache.corner_radius = 6;
	float radius = theme_cache.corner_radius * EDSCALE;

	Color base_color = theme->get_color("base_color", "Editor");
	Color accent_color = theme->get_color("accent_color", "Editor");
	Color font_color = theme->get_color("font_color", "Editor");
	theme_cache.accent_color = accent_color;
	theme_cache.text_muted = Color(font_color.r, font_color.g, font_color.b, 0.6);
	theme_cache.thinking_color = Color(font_color.r, font_color.g, font_color.b, 0.5);

	// User message bubble: subtle accent tint
	theme_cache.message_bg_user.instantiate();
	theme_cache.message_bg_user->set_bg_color(accent_color.lerp(base_color, 0.85));
	theme_cache.message_bg_user->set_corner_radius_all(radius);
	theme_cache.message_bg_user->set_content_margin_all(12 * EDSCALE);

	// Assistant message bubble: slightly lighter than base
	theme_cache.message_bg_assistant.instantiate();
	theme_cache.message_bg_assistant->set_bg_color(base_color.lightened(0.08));
	theme_cache.message_bg_assistant->set_corner_radius_all(radius);
	theme_cache.message_bg_assistant->set_content_margin_all(12 * EDSCALE);

	// Thinking block: subtle, slightly inset appearance
	theme_cache.thinking_bg.instantiate();
	theme_cache.thinking_bg->set_bg_color(base_color.darkened(0.05));
	theme_cache.thinking_bg->set_corner_radius_all(radius * 0.5);
	theme_cache.thinking_bg->set_content_margin_all(8 * EDSCALE);
	theme_cache.thinking_bg->set_border_width(SIDE_LEFT, 2 * EDSCALE);
	theme_cache.thinking_bg->set_border_color(theme_cache.text_muted);

	// Tool card: accent border highlight
	theme_cache.tool_card_bg.instantiate();
	theme_cache.tool_card_bg->set_bg_color(base_color.lightened(0.05));
	theme_cache.tool_card_bg->set_corner_radius_all(radius);
	theme_cache.tool_card_bg->set_border_width(SIDE_LEFT, 3 * EDSCALE);
	theme_cache.tool_card_bg->set_border_color(accent_color);
	theme_cache.tool_card_bg->set_content_margin_all(8 * EDSCALE);
	theme_cache.tool_card_bg->set_content_margin(SIDE_LEFT, 12 * EDSCALE);

	// Input container: soft border
	theme_cache.input_bg.instantiate();
	theme_cache.input_bg->set_bg_color(base_color.darkened(0.05));
	theme_cache.input_bg->set_corner_radius_all(radius);
	theme_cache.input_bg->set_border_width_all(1 * EDSCALE);
	theme_cache.input_bg->set_border_color(base_color.lightened(0.2));
	theme_cache.input_bg->set_content_margin_all(8 * EDSCALE);

	// Token meter pill
	theme_cache.meter_bg.instantiate();
	theme_cache.meter_bg->set_bg_color(base_color.darkened(0.1));
	theme_cache.meter_bg->set_corner_radius_all(12 * EDSCALE);
	theme_cache.meter_bg->set_content_margin(SIDE_LEFT, 10 * EDSCALE);
	theme_cache.meter_bg->set_content_margin(SIDE_RIGHT, 10 * EDSCALE);
	theme_cache.meter_bg->set_content_margin(SIDE_TOP, 4 * EDSCALE);
	theme_cache.meter_bg->set_content_margin(SIDE_BOTTOM, 4 * EDSCALE);

	// Apply to existing components
	if (meter_container) {
		meter_container->add_theme_style_override("panel", theme_cache.meter_bg);
	}
	if (input_container) {
		input_container->add_theme_style_override("panel", theme_cache.input_bg);
	}
}

void AIChatDock::_build_header() {
	header = memnew(HBoxContainer);
	header->set_h_size_flags(SIZE_EXPAND_FILL);
	add_child(header);

	// Token meter in a pill
	meter_container = memnew(PanelContainer);
	header->add_child(meter_container);

	token_meter = memnew(Label);
	token_meter->set_text("Ready");
	token_meter->add_theme_font_size_override("font_size", 11 * EDSCALE);
	meter_container->add_child(token_meter);

	header->add_spacer();

	// New conversation button
	new_btn = memnew(Button);
	new_btn->set_text("New");
	new_btn->set_tooltip_text("Start new conversation");
	new_btn->set_flat(true);
	header->add_child(new_btn);
	new_btn->connect("pressed", callable_mp(this, &AIChatDock::_on_new_conversation));
}

void AIChatDock::_build_messages_area() {
	scroll = memnew(ScrollContainer);
	scroll->set_v_size_flags(SIZE_EXPAND_FILL);
	scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
	scroll->set_vertical_scroll_mode(ScrollContainer::SCROLL_MODE_AUTO);
	add_child(scroll);

	messages_container = memnew(VBoxContainer);
	messages_container->set_h_size_flags(SIZE_EXPAND_FILL);
	messages_container->add_theme_constant_override("separation", 12 * EDSCALE);
	scroll->add_child(messages_container);
}

void AIChatDock::_build_input_area() {
	input_container = memnew(PanelContainer);
	add_child(input_container);

	input_row = memnew(HBoxContainer);
	input_row->add_theme_constant_override("separation", 8 * EDSCALE);
	input_container->add_child(input_row);

	// Multi-line input
	input = memnew(TextEdit);
	input->set_h_size_flags(SIZE_EXPAND_FILL);
	input->set_placeholder("Ask Gameable...");
	input->set_line_wrapping_mode(TextEdit::LINE_WRAPPING_BOUNDARY);
	input->set_custom_minimum_size(Size2(0, 36 * EDSCALE));
	input->set_fit_content_height_enabled(true);

	// Transparent style for cleaner look
	Ref<StyleBoxFlat> input_style;
	input_style.instantiate();
	input_style->set_bg_color(Color(0, 0, 0, 0));
	input_style->set_content_margin_all(4 * EDSCALE);
	input->add_theme_style_override("normal", input_style);
	input->add_theme_style_override("focus", input_style);

	input_row->add_child(input);

	// Send button
	send_btn = memnew(Button);
	send_btn->set_text(U"→");
	send_btn->set_tooltip_text("Send message (Enter)");
	send_btn->set_custom_minimum_size(Size2(36 * EDSCALE, 36 * EDSCALE));
	input_row->add_child(send_btn);

	send_btn->connect("pressed", callable_mp(this, &AIChatDock::_on_send_pressed));
	input->connect("gui_input", callable_mp(this, &AIChatDock::_on_input_gui_input));
}

// === Turn management ===
// Lazily creates the assistant turn container and sub-blocks as needed

void AIChatDock::_ensure_assistant_turn() {
	if (current_turn) {
		return;
	}

	// Create the turn container (holds thinking + response + tools in sequence)
	current_turn = memnew(VBoxContainer);
	current_turn->add_theme_constant_override("separation", 8 * EDSCALE);

	// Wrap in assistant-styled panel
	PanelContainer *wrapper = memnew(PanelContainer);
	wrapper->add_theme_style_override("panel", theme_cache.message_bg_assistant);
	wrapper->add_child(current_turn);

	messages_container->add_child(wrapper);
	_scroll_to_bottom();
}

void AIChatDock::_ensure_thinking_block() {
	_ensure_assistant_turn();

	if (current_thinking_block) {
		return;
	}

	// Create thinking block with subtle styling
	current_thinking_block = memnew(PanelContainer);
	current_thinking_block->add_theme_style_override("panel", theme_cache.thinking_bg);

	VBoxContainer *thinking_content = memnew(VBoxContainer);
	thinking_content->add_theme_constant_override("separation", 2 * EDSCALE);
	current_thinking_block->add_child(thinking_content);

	// "Thinking..." label
	Label *thinking_label = memnew(Label);
	thinking_label->set_text("Thinking...");
	thinking_label->add_theme_font_size_override("font_size", 10 * EDSCALE);
	thinking_label->add_theme_color_override("font_color", theme_cache.text_muted);
	thinking_content->add_child(thinking_label);

	// Streaming text area
	current_thinking_text = memnew(RichTextLabel);
	current_thinking_text->set_autowrap_mode(TextServer::AUTOWRAP_WORD);
	current_thinking_text->set_fit_content(true);
	current_thinking_text->set_selection_enabled(true);
	current_thinking_text->add_theme_color_override("default_color", theme_cache.thinking_color);

	// Use italic/monospace for distinction
	if (EditorNode::get_singleton()) {
		Ref<Theme> theme = EditorNode::get_singleton()->get_editor_theme();
		if (theme.is_valid()) {
			current_thinking_text->add_theme_font_override("normal_font", theme->get_font("source", "EditorFonts"));
			current_thinking_text->add_theme_font_size_override("normal_font_size", 11 * EDSCALE);
		}
	}
	thinking_content->add_child(current_thinking_text);

	// Insert at start of turn (thinking comes first)
	current_turn->add_child(current_thinking_block);
	current_turn->move_child(current_thinking_block, 0);

	has_thinking = true;
}

void AIChatDock::_ensure_response_block() {
	_ensure_assistant_turn();

	if (current_response_text) {
		return;
	}

	current_response_text = memnew(RichTextLabel);
	current_response_text->set_autowrap_mode(TextServer::AUTOWRAP_WORD);
	current_response_text->set_fit_content(true);
	current_response_text->set_use_bbcode(true);
	current_response_text->set_selection_enabled(true);
	current_response_text->set_meta_underline(true);
	current_response_text->connect("meta_clicked", callable_mp(this, &AIChatDock::_on_meta_clicked));

	// Insert after thinking block (if any), before tools
	int idx = has_thinking ? 1 : 0;
	current_turn->add_child(current_response_text);
	current_turn->move_child(current_response_text, idx);

	has_response = true;
}

void AIChatDock::_ensure_tools_container() {
	_ensure_assistant_turn();

	if (current_tools_container) {
		return;
	}

	current_tools_container = memnew(VBoxContainer);
	current_tools_container->add_theme_constant_override("separation", 4 * EDSCALE);

	// Tools always at the end
	current_turn->add_child(current_tools_container);
}

// === Message creation ===

PanelContainer *AIChatDock::_create_user_bubble(const String &p_text) {
	PanelContainer *bubble = memnew(PanelContainer);
	bubble->add_theme_style_override("panel", theme_cache.message_bg_user);

	VBoxContainer *content = memnew(VBoxContainer);
	content->add_theme_constant_override("separation", 4 * EDSCALE);
	bubble->add_child(content);

	// "You" label
	Label *role_label = memnew(Label);
	role_label->set_text("You");
	role_label->add_theme_font_size_override("font_size", 10 * EDSCALE);
	role_label->add_theme_color_override("font_color", theme_cache.text_muted);
	content->add_child(role_label);

	// Message text
	RichTextLabel *text = memnew(RichTextLabel);
	text->set_autowrap_mode(TextServer::AUTOWRAP_WORD);
	text->set_fit_content(true);
	text->set_selection_enabled(true);
	text->append_text(p_text);
	content->add_child(text);

	return bubble;
}

PanelContainer *AIChatDock::_create_tool_card(const String &p_name, const String &p_status) {
	PanelContainer *card = memnew(PanelContainer);
	card->add_theme_style_override("panel", theme_cache.tool_card_bg);

	HBoxContainer *row = memnew(HBoxContainer);
	row->add_theme_constant_override("separation", 8 * EDSCALE);
	card->add_child(row);

	// Tool icon (spinner or checkmark)
	Label *icon = memnew(Label);
	icon->set_text(U"◐"); // Spinner
	icon->set_meta("is_icon", true);
	row->add_child(icon);

	// Tool name + status
	Label *label = memnew(Label);
	label->set_text(p_name + ": " + p_status);
	label->add_theme_font_size_override("font_size", 12 * EDSCALE);
	label->set_meta("is_label", true);
	row->add_child(label);

	return card;
}

// === Event handlers ===

void AIChatDock::_on_send_pressed() {
	String text = input->get_text().strip_edges();
	if (text.is_empty()) {
		return;
	}

	// End any previous turn
	end_turn();

	// Add user message
	append_user_message(text);

	// Send to agent
	if (EditorAIAgent *agent = EditorAIAgent::get_singleton()) {
		agent->request_chat(text);
	}

	input->clear();
}

void AIChatDock::_on_input_gui_input(const Ref<InputEvent> &p_event) {
	Ref<InputEventKey> key = p_event;
	if (key.is_valid() && key->is_pressed() && !key->is_echo()) {
		if (key->get_keycode() == Key::ENTER && !key->is_shift_pressed()) {
			_on_send_pressed();
			input->accept_event();
		}
	}
}

void AIChatDock::_on_new_conversation() {
	clear_all();
	if (EditorAIAgent *agent = EditorAIAgent::get_singleton()) {
		agent->clear_session();
	}
}

void AIChatDock::_on_meta_clicked(const Variant &p_meta) {
	if (p_meta.get_type() != Variant::STRING) {
		return;
	}

	String path = p_meta;
	if (path.is_empty()) {
		return;
	}

	// Navigate to file/resource
	if (path.ends_with(".tscn") || path.ends_with(".scn")) {
		EditorInterface::get_singleton()->open_scene_from_path(path);
	} else {
		Ref<Resource> res = ResourceLoader::load(path);
		if (res.is_valid()) {
			EditorInterface::get_singleton()->edit_resource(res);
		} else {
			EditorInterface::get_singleton()->select_file(path);
		}
	}
}

// === Agent signal handlers ===

void AIChatDock::_on_thinking(const String &p_text) {
	append_thinking(p_text);
}

void AIChatDock::_on_status(const String &p_level, const String &p_message) {
	if (p_message == "chat:end") {
		end_turn();
	}
}

void AIChatDock::_on_usage_updated(int64_t p_turn, int64_t p_session) {
	update_usage(p_turn, p_session);
}

void AIChatDock::_on_tool_call(const String &p_id, const String &p_name, const Dictionary &p_input) {
	tool_call_count++;
	add_tool_card(p_id, p_name, "running...");
	_update_meter();
}

void AIChatDock::_on_tool_result(const String &p_id, bool p_ok, const Dictionary &p_output) {
	update_tool_card(p_id, p_ok ? "done" : "failed", true);
}

void AIChatDock::_on_tool_progress(const String &p_id, const String &p_stage, float p_progress) {
	update_tool_card(p_id, p_stage, false);
}

void AIChatDock::_on_chat_message(const String &p_role, const String &p_text) {
	if (p_role == "You" || p_role == "user") {
		// User message already handled in _on_send_pressed
		return;
	}
	// Assistant response - stream into current turn
	append_response(p_text);
}

void AIChatDock::_update_meter() {
	String text;
	if (turn_tokens > 0 || session_tokens > 0) {
		text = "Turn: " + itos(turn_tokens);
		if (session_tokens > 0) {
			text += " | Session: " + itos(session_tokens);
		}
		if (tool_call_count > 0) {
			text += " | Tools: " + itos(tool_call_count);
		}
	} else {
		text = "Ready";
	}
	token_meter->set_text(text);
}

void AIChatDock::_scroll_to_bottom() {
	callable_mp(this, &AIChatDock::_do_scroll_to_bottom).call_deferred();
}

void AIChatDock::_do_scroll_to_bottom() {
	if (scroll && scroll->get_v_scroll_bar()) {
		scroll->set_v_scroll(scroll->get_v_scroll_bar()->get_max());
	}
}

// === Public API ===

void AIChatDock::append_user_message(const String &p_text) {
	// End any previous assistant turn
	end_turn();

	PanelContainer *bubble = _create_user_bubble(p_text);
	messages_container->add_child(bubble);
	_scroll_to_bottom();
}

void AIChatDock::append_thinking(const String &p_text) {
	_ensure_thinking_block();
	current_thinking_text->append_text(p_text);
	_scroll_to_bottom();
}

void AIChatDock::append_response(const String &p_text) {
	_ensure_response_block();
	current_response_text->append_text(p_text);
	_scroll_to_bottom();
}

void AIChatDock::add_tool_card(const String &p_id, const String &p_name, const String &p_status) {
	_ensure_tools_container();

	PanelContainer *card = _create_tool_card(p_name, p_status);
	current_tools_container->add_child(card);
	active_tool_cards[p_id] = card;
	_scroll_to_bottom();
}

void AIChatDock::update_tool_card(const String &p_id, const String &p_status, bool p_done) {
	if (!active_tool_cards.has(p_id)) {
		return;
	}

	PanelContainer *card = active_tool_cards[p_id];
	HBoxContainer *row = Object::cast_to<HBoxContainer>(card->get_child(0));
	if (!row) {
		return;
	}

	// Update icon and label
	for (int i = 0; i < row->get_child_count(); i++) {
		Node *child = row->get_child(i);
		if (child->has_meta("is_icon")) {
			Label *icon = Object::cast_to<Label>(child);
			if (icon) {
				icon->set_text(p_done ? U"✓" : U"◐");
			}
		}
		if (child->has_meta("is_label")) {
			Label *label = Object::cast_to<Label>(child);
			if (label) {
				// Extract tool name (before colon)
				String current = label->get_text();
				int colon_pos = current.find(":");
				String name = colon_pos >= 0 ? current.substr(0, colon_pos) : current;
				label->set_text(name + ": " + p_status);
			}
		}
	}

	if (p_done) {
		// Mark for cleanup
		tool_done_times[p_id] = OS::get_singleton()->get_ticks_msec();

		// Change border to green for success
		Ref<StyleBoxFlat> done_style = theme_cache.tool_card_bg->duplicate();
		done_style->set_border_color(Color(0.4, 0.8, 0.4));
		card->add_theme_style_override("panel", done_style);
	}
}

void AIChatDock::update_usage(int64_t p_turn, int64_t p_session) {
	turn_tokens = p_turn;
	session_tokens = p_session;
	_update_meter();
}

void AIChatDock::cleanup_done_tools() {
	// Keep completed tools visible for a bit, then fade them
	// (For now, just leave them - they're part of the conversation history)
}

void AIChatDock::end_turn() {
	// Clear streaming state - turn container stays in messages
	current_turn = nullptr;
	current_thinking_block = nullptr;
	current_thinking_text = nullptr;
	current_response_text = nullptr;
	current_tools_container = nullptr;
	has_thinking = false;
	has_response = false;

	// Reset per-turn tracking
	turn_tokens = 0;
	tool_call_count = 0;
	active_tool_cards.clear();
	tool_done_times.clear();
	_update_meter();
}

void AIChatDock::clear_all() {
	// Remove all messages
	for (int i = messages_container->get_child_count() - 1; i >= 0; i--) {
		messages_container->get_child(i)->queue_free();
	}

	// Reset all state
	end_turn();
	session_tokens = 0;
	_update_meter();
}
