/**************************************************************************/
/*  ai_chat_dock.cpp                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GAMEABLE                                   */
/*                     AI-powered game development                        */
/**************************************************************************/

#include "ai_chat_dock.h"

#include "core/input/input.h"
#include "core/input/input_event.h"
#include "core/io/resource_loader.h"
#include "core/os/os.h"
#include "editor/ai/editor_ai_agent.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/rich_text_label.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/separator.h"
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
	_build_tool_progress_area();
	_build_messages_area();
	_build_reasoning_section();
	_build_input_area();
}

void AIChatDock::_build_styles() {
	// Get editor theme properties
	Ref<Theme> theme = EditorNode::get_singleton() ? EditorNode::get_singleton()->get_editor_theme() : nullptr;
	if (!theme.is_valid()) {
		return;
	}

	// Corner radius from editor settings
	theme_cache.corner_radius = EDITOR_GET("interface/theme/corner_radius");
	float radius = theme_cache.corner_radius * EDSCALE;

	// Colors from theme
	Color base_color = theme->get_color("base_color", "Editor");
	Color accent_color = theme->get_color("accent_color", "Editor");
	Color font_color = theme->get_color("font_color", "Editor");
	theme_cache.accent_color = accent_color;
	theme_cache.text_muted = Color(font_color.r, font_color.g, font_color.b, 0.6);

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

	// Reasoning panel: dark, subtle
	theme_cache.reasoning_bg.instantiate();
	theme_cache.reasoning_bg->set_bg_color(base_color.darkened(0.15));
	theme_cache.reasoning_bg->set_corner_radius_all(radius);
	theme_cache.reasoning_bg->set_content_margin_all(10 * EDSCALE);

	// Tool progress card: accent border highlight
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

	// Apply styles to existing components
	if (meter_container) {
		meter_container->add_theme_style_override("panel", theme_cache.meter_bg);
	}
	if (input_container) {
		input_container->add_theme_style_override("panel", theme_cache.input_bg);
	}
	if (reasoning_panel) {
		reasoning_panel->add_theme_style_override("panel", theme_cache.reasoning_bg);
	}
}

void AIChatDock::_build_header() {
	header = memnew(HBoxContainer);
	header->set_h_size_flags(SIZE_EXPAND_FILL);
	add_child(header);

	// Token meter in a pill-shaped container
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

void AIChatDock::_build_tool_progress_area() {
	tool_progress_area = memnew(VBoxContainer);
	tool_progress_area->add_theme_constant_override("separation", 6 * EDSCALE);
	add_child(tool_progress_area);
}

void AIChatDock::_build_messages_area() {
	scroll = memnew(ScrollContainer);
	scroll->set_v_size_flags(SIZE_EXPAND_FILL);
	scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
	scroll->set_vertical_scroll_mode(ScrollContainer::SCROLL_MODE_AUTO);
	add_child(scroll);

	messages_container = memnew(VBoxContainer);
	messages_container->set_h_size_flags(SIZE_EXPAND_FILL);
	messages_container->add_theme_constant_override("separation", 8 * EDSCALE);
	scroll->add_child(messages_container);
}

void AIChatDock::_build_reasoning_section() {
	reasoning_section = memnew(VBoxContainer);
	add_child(reasoning_section);

	// Toggle button (collapsed by default)
	reasoning_toggle = memnew(Button);
	reasoning_toggle->set_text(U"▶ Reasoning");
	reasoning_toggle->set_text_alignment(HORIZONTAL_ALIGNMENT_LEFT);
	reasoning_toggle->set_flat(true);
	reasoning_toggle->add_theme_color_override("font_color", theme_cache.text_muted);
	reasoning_section->add_child(reasoning_toggle);
	reasoning_toggle->connect("pressed", callable_mp(this, &AIChatDock::_on_reasoning_toggle));

	// Collapsible panel
	reasoning_panel = memnew(PanelContainer);
	reasoning_panel->set_visible(false);
	reasoning_section->add_child(reasoning_panel);

	reasoning_text = memnew(RichTextLabel);
	reasoning_text->set_autowrap_mode(TextServer::AUTOWRAP_WORD);
	reasoning_text->set_fit_content(true);
	reasoning_text->set_selection_enabled(true);
	reasoning_text->set_custom_minimum_size(Size2(0, 0));

	// Use monospace font for reasoning
	if (EditorNode::get_singleton()) {
		Ref<Theme> theme = EditorNode::get_singleton()->get_editor_theme();
		if (theme.is_valid()) {
			reasoning_text->add_theme_font_override("normal_font", theme->get_font("source", "EditorFonts"));
			reasoning_text->add_theme_font_size_override("normal_font_size", 12 * EDSCALE);
		}
	}
	reasoning_text->add_theme_color_override("default_color", theme_cache.text_muted);
	reasoning_panel->add_child(reasoning_text);
}

void AIChatDock::_build_input_area() {
	input_container = memnew(PanelContainer);
	add_child(input_container);

	input_row = memnew(HBoxContainer);
	input_row->add_theme_constant_override("separation", 8 * EDSCALE);
	input_container->add_child(input_row);

	// Multi-line input with auto-grow
	input = memnew(TextEdit);
	input->set_h_size_flags(SIZE_EXPAND_FILL);
	input->set_placeholder("Ask Gameable...");
	input->set_line_wrapping_mode(TextEdit::LINE_WRAPPING_BOUNDARY);
	input->set_custom_minimum_size(Size2(0, 36 * EDSCALE));
	input->set_fit_content_height_enabled(true);

	// Make input look cleaner
	Ref<StyleBoxFlat> input_style;
	input_style.instantiate();
	input_style->set_bg_color(Color(0, 0, 0, 0)); // Transparent
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

PanelContainer *AIChatDock::_create_message_bubble(const String &p_role, const String &p_text) {
	PanelContainer *bubble = memnew(PanelContainer);

	// Style based on role
	if (p_role == "You" || p_role == "user") {
		bubble->add_theme_style_override("panel", theme_cache.message_bg_user);
	} else {
		bubble->add_theme_style_override("panel", theme_cache.message_bg_assistant);
	}

	VBoxContainer *content = memnew(VBoxContainer);
	content->add_theme_constant_override("separation", 4 * EDSCALE);
	bubble->add_child(content);

	// Role label (small, muted)
	Label *role_label = memnew(Label);
	role_label->set_text(p_role);
	role_label->add_theme_font_size_override("font_size", 10 * EDSCALE);
	role_label->add_theme_color_override("font_color", theme_cache.text_muted);
	content->add_child(role_label);

	// Message text with BBCode support
	RichTextLabel *text = memnew(RichTextLabel);
	text->set_autowrap_mode(TextServer::AUTOWRAP_WORD);
	text->set_fit_content(true);
	text->set_use_bbcode(true);
	text->set_selection_enabled(true);
	text->set_meta_underline(true);
	text->append_text(p_text);
	text->connect("meta_clicked", callable_mp(this, &AIChatDock::_on_meta_clicked));
	content->add_child(text);

	return bubble;
}

void AIChatDock::_on_send_pressed() {
	String text = input->get_text().strip_edges();
	if (text.is_empty()) {
		return;
	}

	reset_turn_state();

	if (EditorAIAgent *agent = EditorAIAgent::get_singleton()) {
		agent->request_chat(text);
	}

	input->clear();
}

void AIChatDock::_on_input_gui_input(const Ref<InputEvent> &p_event) {
	// Handle Enter without Shift to submit
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

void AIChatDock::_on_reasoning_toggle() {
	reasoning_collapsed = !reasoning_collapsed;
	reasoning_panel->set_visible(!reasoning_collapsed);
	reasoning_toggle->set_text(reasoning_collapsed ? U"▶ Reasoning" : U"▼ Reasoning");

	if (!reasoning_collapsed && !current_reasoning.is_empty()) {
		// Limit max height
		float max_height = 200 * EDSCALE;
		float content_height = reasoning_text->get_content_height();
		reasoning_panel->set_custom_minimum_size(Size2(0, MIN(max_height, content_height + 20 * EDSCALE)));
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

	// Open file in appropriate editor
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

// Agent signal handlers
void AIChatDock::_on_thinking(const String &p_text) {
	append_thinking(p_text);
}

void AIChatDock::_on_status(const String &p_level, const String &p_message) {
	if (p_message == "chat:start") {
		reset_turn_state();
	}
}

void AIChatDock::_on_usage_updated(int64_t p_turn, int64_t p_session) {
	update_usage(p_turn, p_session);
}

void AIChatDock::_on_tool_call(const String &p_id, const String &p_name, const Dictionary &p_input) {
	tool_call_count++;
	add_tool_progress(p_id, p_name, "running");
	_update_meter();
}

void AIChatDock::_on_tool_result(const String &p_id, bool p_ok, const Dictionary &p_output) {
	mark_tool_done(p_id);
}

void AIChatDock::_on_tool_progress(const String &p_id, const String &p_stage, float p_progress) {
	// Update existing card if present
	if (active_tool_cards.has(p_id)) {
		PanelContainer *card = active_tool_cards[p_id];
		if (Label *lbl = Object::cast_to<Label>(card->get_child(0))) {
			lbl->set_text(p_stage);
		}
	}
}

void AIChatDock::_on_chat_message(const String &p_role, const String &p_text) {
	append_message(p_role, p_text);
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
	// Defer to next frame so layout is updated
	callable_mp(this, &AIChatDock::_do_scroll_to_bottom).call_deferred();
}

void AIChatDock::_do_scroll_to_bottom() {
	if (scroll && scroll->get_v_scroll_bar()) {
		scroll->set_v_scroll(scroll->get_v_scroll_bar()->get_max());
	}
}

// Public API
void AIChatDock::append_message(const String &p_role, const String &p_text) {
	PanelContainer *bubble = _create_message_bubble(p_role, p_text);
	messages_container->add_child(bubble);
	_scroll_to_bottom();
}

void AIChatDock::append_assistant_chunk(const String &p_text) {
	// For streaming: append to last assistant message or create new one
	int child_count = messages_container->get_child_count();
	if (child_count > 0) {
		if (PanelContainer *last = Object::cast_to<PanelContainer>(messages_container->get_child(child_count - 1))) {
			if (VBoxContainer *content = Object::cast_to<VBoxContainer>(last->get_child(0))) {
				for (int i = 0; i < content->get_child_count(); i++) {
					if (RichTextLabel *rtl = Object::cast_to<RichTextLabel>(content->get_child(i))) {
						rtl->append_text(p_text);
						_scroll_to_bottom();
						return;
					}
				}
			}
		}
	}

	// No existing message, create new
	append_message("Assistant", p_text);
}

void AIChatDock::append_thinking(const String &p_text) {
	current_reasoning += p_text;
	reasoning_text->append_text(p_text);

	// Show toggle if reasoning exists
	reasoning_toggle->set_visible(!current_reasoning.is_empty());
}

void AIChatDock::update_usage(int64_t p_turn, int64_t p_session) {
	turn_tokens = p_turn;
	session_tokens = p_session;
	_update_meter();
}

void AIChatDock::add_tool_progress(const String &p_id, const String &p_name, const String &p_stage) {
	PanelContainer *card = nullptr;

	if (active_tool_cards.has(p_id)) {
		card = active_tool_cards[p_id];
		if (Label *lbl = Object::cast_to<Label>(card->get_child(0))) {
			lbl->set_text(p_name + ": " + p_stage);
		}
	} else {
		card = memnew(PanelContainer);
		card->add_theme_style_override("panel", theme_cache.tool_card_bg);

		Label *lbl = memnew(Label);
		lbl->set_text(p_name + ": " + p_stage);
		lbl->add_theme_font_size_override("font_size", 12 * EDSCALE);
		card->add_child(lbl);

		tool_progress_area->add_child(card);
		active_tool_cards[p_id] = card;
	}
}

void AIChatDock::mark_tool_done(const String &p_id) {
	tool_done_times[p_id] = OS::get_singleton()->get_ticks_msec();

	// Update card to show done state
	if (active_tool_cards.has(p_id)) {
		PanelContainer *card = active_tool_cards[p_id];
		// Fade to success color
		Ref<StyleBoxFlat> done_style = theme_cache.tool_card_bg->duplicate();
		done_style->set_border_color(Color(0.4, 0.8, 0.4)); // Green accent
		card->add_theme_style_override("panel", done_style);
	}
}

void AIChatDock::cleanup_done_tools() {
	uint64_t now = OS::get_singleton()->get_ticks_msec();
	Vector<String> to_remove;

	for (const KeyValue<String, uint64_t> &kv : tool_done_times) {
		if (now - kv.value > 1500) { // Fade out after 1.5s
			to_remove.push_back(kv.key);
		}
	}

	for (const String &id : to_remove) {
		if (active_tool_cards.has(id)) {
			active_tool_cards[id]->queue_free();
			active_tool_cards.erase(id);
		}
		tool_done_times.erase(id);
	}
}

void AIChatDock::reset_turn_state() {
	turn_tokens = 0;
	tool_call_count = 0;
	current_reasoning = "";
	reasoning_text->clear();
	reasoning_toggle->set_visible(false);
	reasoning_panel->set_visible(false);
	reasoning_collapsed = true;
	_update_meter();
}

void AIChatDock::clear_all() {
	// Remove all message bubbles
	for (int i = messages_container->get_child_count() - 1; i >= 0; i--) {
		messages_container->get_child(i)->queue_free();
	}

	// Clear tool cards
	for (KeyValue<String, PanelContainer *> &kv : active_tool_cards) {
		kv.value->queue_free();
	}
	active_tool_cards.clear();
	tool_done_times.clear();

	// Reset state
	reset_turn_state();
	session_tokens = 0;
	_update_meter();
}
