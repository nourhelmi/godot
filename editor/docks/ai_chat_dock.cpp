/**************************************************************************/
/*  ai_chat_dock.cpp                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GAMEABLE                                   */
/*                     AI-powered game development                        */
/**************************************************************************/

#include "ai_chat_dock.h"

#include "ai_mention_popup.h"
#include "core/config/project_settings.h"
#include "core/core_bind.h"
#include "core/input/input_event.h"
#include "core/io/json.h"
#include "core/io/resource_loader.h"
#include "core/os/os.h"
#include "editor/ai/editor_ai_agent.h"
#include "editor/ai/editor_ai_types.h"
#include "editor/editor_interface.h"
#include "editor/editor_main_screen.h"
#include "editor/editor_node.h"
#include "editor/scene/3d/node_3d_editor_plugin.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/flow_container.h"
#include "scene/gui/label.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/rich_text_label.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/text_edit.h"
#include "scene/gui/texture_rect.h"
#include "scene/main/node.h"
#include "scene/main/viewport.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/style_box_flat.h"
#include "servers/display/display_server.h"

using CoreBind::Marshalls;

// Convert markdown to BBCode for RichTextLabel display
// Handles: code blocks, inline code, bold, italic, headers, links
static String _markdown_to_bbcode(const String &p_markdown) {
	String result;
	String text = p_markdown;
	int pos = 0;
	int len = text.length();

	// State for streaming - we track if we're inside formatting contexts
	bool in_code_block = false;
	String code_block_lang;

	while (pos < len) {
		// Check for code block start/end (```)
		if (pos + 2 < len && text[pos] == '`' && text[pos + 1] == '`' && text[pos + 2] == '`') {
			if (!in_code_block) {
				// Start of code block - find language identifier
				int line_end = text.find("\n", pos + 3);
				if (line_end == -1) {
					line_end = len;
				}
				code_block_lang = text.substr(pos + 3, line_end - (pos + 3)).strip_edges();
				in_code_block = true;
				result += "[code]";
				pos = line_end + 1;
				continue;
			} else {
				// End of code block
				in_code_block = false;
				result += "[/code]";
				pos += 3;
				// Skip trailing newline if present
				if (pos < len && text[pos] == '\n') {
					pos++;
				}
				continue;
			}
		}

		// Inside code block - pass through literally (no formatting)
		if (in_code_block) {
			result += text[pos];
			pos++;
			continue;
		}

		// Inline code (single backtick)
		if (text[pos] == '`') {
			int end = text.find("`", pos + 1);
			if (end != -1) {
				result += "[code]" + text.substr(pos + 1, end - pos - 1) + "[/code]";
				pos = end + 1;
				continue;
			}
		}

		// Bold (**text** or __text__)
		if (pos + 1 < len && ((text[pos] == '*' && text[pos + 1] == '*') || (text[pos] == '_' && text[pos + 1] == '_'))) {
			char32_t marker = text[pos];
			int end = text.find(String::chr(marker) + String::chr(marker), pos + 2);
			if (end != -1) {
				result += "[b]" + _markdown_to_bbcode(text.substr(pos + 2, end - pos - 2)) + "[/b]";
				pos = end + 2;
				continue;
			}
		}

		// Italic (*text* or _text_) - but not when preceded/followed by word char for _
		if ((text[pos] == '*' || text[pos] == '_') && (pos + 1 < len && text[pos + 1] != text[pos])) {
			char32_t marker = text[pos];
			// For underscore, be more careful to avoid matching mid-word
			if (marker == '_') {
				bool valid_start = (pos == 0 || !is_ascii_alphanumeric_char(text[pos - 1]));
				if (!valid_start) {
					result += text[pos];
					pos++;
					continue;
				}
			}
			int end = text.find_char(marker, pos + 1);
			if (end != -1 && end > pos + 1) {
				// For underscore, check valid end
				if (marker == '_' && end + 1 < len && is_ascii_alphanumeric_char(text[end + 1])) {
					result += text[pos];
					pos++;
					continue;
				}
				result += "[i]" + _markdown_to_bbcode(text.substr(pos + 1, end - pos - 1)) + "[/i]";
				pos = end + 1;
				continue;
			}
		}

		// Headers at start of line (# ## ###)
		if (text[pos] == '#' && (pos == 0 || text[pos - 1] == '\n')) {
			int header_level = 0;
			int h_pos = pos;
			while (h_pos < len && text[h_pos] == '#' && header_level < 6) {
				header_level++;
				h_pos++;
			}
			if (h_pos < len && text[h_pos] == ' ') {
				h_pos++; // skip space after #
				int line_end = text.find("\n", h_pos);
				if (line_end == -1) {
					line_end = len;
				}
				String header_text = text.substr(h_pos, line_end - h_pos);
				// Use font size based on header level
				int size = 24 - (header_level - 1) * 2; // h1=24, h2=22, h3=20...
				result += "[font_size=" + itos(size) + "][b]" + _markdown_to_bbcode(header_text) + "[/b][/font_size]\n";
				pos = line_end + 1;
				continue;
			}
		}

		// Links [text](url)
		if (text[pos] == '[') {
			int bracket_end = text.find("]", pos + 1);
			if (bracket_end != -1 && bracket_end + 1 < len && text[bracket_end + 1] == '(') {
				int paren_end = text.find(")", bracket_end + 2);
				if (paren_end != -1) {
					String link_text = text.substr(pos + 1, bracket_end - pos - 1);
					String url = text.substr(bracket_end + 2, paren_end - bracket_end - 2);
					result += "[url=" + url + "]" + link_text + "[/url]";
					pos = paren_end + 1;
					continue;
				}
			}
		}

		// List items (- or * at start of line)
		if ((text[pos] == '-' || text[pos] == '*') && (pos == 0 || text[pos - 1] == '\n')) {
			if (pos + 1 < len && text[pos + 1] == ' ') {
				result += "• ";
				pos += 2;
				continue;
			}
		}

		// Default: pass through character
		result += text[pos];
		pos++;
	}

	return result;
}

static String _shorten_path(const String &p_path) {
	String root = ProjectSettings::get_singleton()->get_resource_path();
	if (root.is_empty()) {
		return p_path;
	}
	if (p_path.begins_with(root)) {
		String rel = p_path.substr(root.length());
		if (rel.begins_with("/")) {
			rel = rel.substr(1);
		}
		return "res://" + rel;
	}
	return p_path;
}

static String _collapse_whitespace(const String &p_text) {
	String out;
	bool in_space = false;
	for (int i = 0; i < p_text.length(); i++) {
		const char32_t c = p_text[i];
		if (c == '\n' || c == '\r' || c == '\t' || c == ' ') {
			if (!in_space) {
				out += " ";
				in_space = true;
			}
			continue;
		}
		in_space = false;
		out += c;
	}
	return out.strip_edges();
}

static String _truncate_text(const String &p_text, int p_max_chars) {
	if (p_max_chars <= 0) {
		return String();
	}
	if (p_text.length() <= p_max_chars) {
		return p_text;
	}
	return p_text.substr(0, p_max_chars - 3) + "...";
}

static String _format_task_stream(const Dictionary &p_message) {
	if (!p_message.has("parts")) {
		return String();
	}
	Variant parts_var = p_message["parts"];
	if (parts_var.get_type() != Variant::ARRAY) {
		return String();
	}
	Array parts = parts_var;
	if (parts.is_empty()) {
		return String();
	}

	String out;
	for (int i = 0; i < parts.size(); i++) {
		if (parts[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		Dictionary part = parts[i];
		String type = part.has("type") ? String(part["type"]) : String();
		if (type == "text") {
			String text = part.has("text") ? String(part["text"]) : String();
			if (!text.is_empty()) {
				out += text;
				if (!text.ends_with("\n")) {
					out += "\n";
				}
			}
			continue;
		}
		if (type.begins_with("tool-")) {
			const String tool_name = type.substr(5);
			const String state = part.has("state") ? String(part["state"]) : String();
			out += "[tool] " + (tool_name.is_empty() ? String("tool") : tool_name);
			if (!state.is_empty()) {
				out += " (" + state + ")";
			}
			out += "\n";
			if (part.has("input")) {
				const String json = JSON::stringify(part["input"]);
				out += "  input: " + _truncate_text(json, 2000) + "\n";
			}
			if (part.has("output")) {
				const String json = JSON::stringify(part["output"]);
				out += "  output: " + _truncate_text(json, 2000) + "\n";
			}
			continue;
		}
		if (!type.is_empty()) {
			out += "[part] " + type;
			if (part.has("text")) {
				out += ": " + String(part["text"]);
			}
			out += "\n";
		}
	}

	return out.strip_edges();
}

static void _count_scene_node_types(Node *p_node, Node *p_root, int &r_2d, int &r_3d) {
	if (!p_node || !p_root) {
		return;
	}
	if (p_node->is_class("Viewport") || (p_node != p_root && p_node->get_owner() != p_root)) {
		return;
	}

	if (p_node->is_class("CanvasItem")) {
		r_2d++;
	} else if (p_node->is_class("Node3D")) {
		r_3d++;
	}

	for (int i = 0; i < p_node->get_child_count(); i++) {
		_count_scene_node_types(p_node->get_child(i), p_root, r_2d, r_3d);
	}
}

void AIChatDock::_bind_methods() {
	// No exposed methods yet
}

void AIChatDock::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			_build_styles();

			// Connect to AI agent signals (guard against duplicate connections on re-parenting)
			if (EditorAIAgent *agent = EditorAIAgent::get_singleton()) {
				Callable thinking_cb = callable_mp(this, &AIChatDock::_on_thinking);
				Callable status_cb = callable_mp(this, &AIChatDock::_on_status);
				Callable usage_cb = callable_mp(this, &AIChatDock::_on_usage_updated);
				Callable tool_call_cb = callable_mp(this, &AIChatDock::_on_tool_call);
				Callable tool_result_cb = callable_mp(this, &AIChatDock::_on_tool_result);
				Callable tool_progress_cb = callable_mp(this, &AIChatDock::_on_tool_progress);
				Callable chat_message_cb = callable_mp(this, &AIChatDock::_on_chat_message);
				Callable context_updated_cb = callable_mp(this, &AIChatDock::_on_context_updated);
				Callable processing_cb = callable_mp(this, &AIChatDock::_on_processing_state_changed);

				if (!agent->is_connected("thinking", thinking_cb)) {
					agent->connect("thinking", thinking_cb);
				}
				if (!agent->is_connected("status", status_cb)) {
					agent->connect("status", status_cb);
				}
				if (!agent->is_connected("usage_updated", usage_cb)) {
					agent->connect("usage_updated", usage_cb);
				}
				if (!agent->is_connected("tool_call", tool_call_cb)) {
					agent->connect("tool_call", tool_call_cb);
				}
				if (!agent->is_connected("tool_result", tool_result_cb)) {
					agent->connect("tool_result", tool_result_cb);
				}
				if (!agent->is_connected("tool_progress", tool_progress_cb)) {
					agent->connect("tool_progress", tool_progress_cb);
				}
				if (!agent->is_connected("chat_message", chat_message_cb)) {
					agent->connect("chat_message", chat_message_cb);
				}
				if (!agent->is_connected("context_updated", context_updated_cb)) {
					agent->connect("context_updated", context_updated_cb);
				}
				if (!agent->is_connected("processing_state_changed", processing_cb)) {
					agent->connect("processing_state_changed", processing_cb);
				}

				// Initialize button state based on current processing state
				_on_processing_state_changed(agent->get_is_processing());
			}

			// Sticky autoscroll: only follow new content if the user is already at the bottom.
			if (scroll && scroll->get_v_scroll_bar()) {
				Callable scroll_cb = callable_mp(this, &AIChatDock::_on_scroll_value_changed);
				if (!scroll->get_v_scroll_bar()->is_connected("value_changed", scroll_cb)) {
					scroll->get_v_scroll_bar()->connect("value_changed", scroll_cb);
				}
				_on_scroll_value_changed(scroll->get_v_scroll_bar()->get_value());
			}
		} break;

		case NOTIFICATION_THEME_CHANGED: {
			_build_styles();
		} break;

		case NOTIFICATION_PROCESS: {
			_update_spinner_icons();
			cleanup_done_tools();
		} break;
	}
}

AIChatDock::AIChatDock() {
	set_name("Chat");
	set_h_size_flags(SIZE_EXPAND_FILL);
	set_v_size_flags(SIZE_EXPAND_FILL);
	set_process(true);
	_build_ui();
}

AIChatDock::~AIChatDock() {
}

void AIChatDock::_build_ui() {
	_build_header();
	_build_messages_area();
	_build_pinned_chips_area();
	_build_context_chips_area();
	_build_attachment_chips_area();
	_build_input_area();

	// @ mention popup (Control-based, not Window, to avoid focus issues)
	mention_popup = memnew(AIMentionPopup);
	mention_popup->connect("mention_selected", callable_mp(this, &AIChatDock::_on_mention_selected));
	mention_popup->set_z_index(100); // Render on top
	add_child(mention_popup);
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
	if (capture_btn) {
		capture_btn->set_button_icon(theme->get_icon("Camera", "EditorIcons"));
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

	// Capture viewport button
	capture_btn = memnew(Button);
	capture_btn->set_tooltip_text("Capture viewport screenshot (Cmd/Ctrl+Shift+P)");
	capture_btn->set_custom_minimum_size(Size2(36 * EDSCALE, 36 * EDSCALE));
	capture_btn->set_flat(true);
	input_row->add_child(capture_btn);
	capture_btn->connect("pressed", callable_mp(this, &AIChatDock::_on_capture_pressed));
	capture_btn->set_shortcut(
			ED_SHORTCUT("gameable/capture_viewport", TTRC("Capture Viewport Screenshot"),
					KeyModifierMask::CMD_OR_CTRL | KeyModifierMask::SHIFT | Key::P));

	// Send button (shown when not processing)
	send_btn = memnew(Button);
	send_btn->set_text(U"→");
	send_btn->set_tooltip_text("Send message (Enter)");
	send_btn->set_custom_minimum_size(Size2(36 * EDSCALE, 36 * EDSCALE));
	input_row->add_child(send_btn);
	send_btn->connect("pressed", callable_mp(this, &AIChatDock::_on_send_pressed));

	// Stop button (shown when processing)
	stop_btn = memnew(Button);
	stop_btn->set_text(U"■");
	stop_btn->set_tooltip_text("Stop generation");
	stop_btn->set_custom_minimum_size(Size2(36 * EDSCALE, 36 * EDSCALE));
	stop_btn->set_visible(false); // Hidden initially
	input_row->add_child(stop_btn);
	stop_btn->connect("pressed", callable_mp(this, &AIChatDock::_on_stop_pressed));

	input->connect("gui_input", callable_mp(this, &AIChatDock::_on_input_gui_input));
	input->connect("text_changed", callable_mp(this, &AIChatDock::_on_input_text_changed));
}

void AIChatDock::_build_context_chips_area() {
	// FlowContainer for showing @ mentioned items as chips above input
	context_chips = memnew(FlowContainer);
	context_chips->set_h_size_flags(SIZE_EXPAND_FILL);
	context_chips->add_theme_constant_override("h_separation", 4 * EDSCALE);
	context_chips->add_theme_constant_override("v_separation", 4 * EDSCALE);
	context_chips->set_visible(false); // hidden until items added
	add_child(context_chips);
}

void AIChatDock::_build_attachment_chips_area() {
	attachment_chips = memnew(FlowContainer);
	attachment_chips->set_h_size_flags(SIZE_EXPAND_FILL);
	attachment_chips->add_theme_constant_override("h_separation", 4 * EDSCALE);
	attachment_chips->add_theme_constant_override("v_separation", 4 * EDSCALE);
	attachment_chips->set_visible(false);
	add_child(attachment_chips);
}

void AIChatDock::_build_pinned_chips_area() {
	pinned_chips = memnew(FlowContainer);
	pinned_chips->set_h_size_flags(SIZE_EXPAND_FILL);
	pinned_chips->add_theme_constant_override("h_separation", 4 * EDSCALE);
	pinned_chips->add_theme_constant_override("v_separation", 4 * EDSCALE);
	pinned_chips->set_visible(false); // hidden until items added
	add_child(pinned_chips);
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

	// Collapsible header row
	HBoxContainer *header_row = memnew(HBoxContainer);
	thinking_content->add_child(header_row);

	// Toggle button (triangle indicator)
	current_thinking_toggle = memnew(Button);
	current_thinking_toggle->set_text(U"▼ Thinking...");
	current_thinking_toggle->set_flat(true);
	current_thinking_toggle->add_theme_font_size_override("font_size", 10 * EDSCALE);
	current_thinking_toggle->add_theme_color_override("font_color", theme_cache.text_muted);
	current_thinking_toggle->set_tooltip_text("Click to collapse/expand");
	current_thinking_toggle->connect("pressed", callable_mp(this, &AIChatDock::_on_thinking_toggle));
	header_row->add_child(current_thinking_toggle);

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

	// Insert before response if it already started to keep thinking-first ordering.
	if (current_response_text && current_response_text->get_parent() == current_turn) {
		current_turn->add_child(current_thinking_block);
		current_turn->move_child(current_thinking_block, current_response_text->get_index());
	} else {
		current_turn->add_child(current_thinking_block);
	}

	thinking_collapsed = false;
	has_thinking = true;
}

void AIChatDock::_on_thinking_toggle() {
	if (!current_thinking_text || !current_thinking_toggle) {
		return;
	}

	thinking_collapsed = !thinking_collapsed;
	current_thinking_text->set_visible(!thinking_collapsed);

	if (thinking_collapsed) {
		current_thinking_toggle->set_text(U"▶ Thinking... (collapsed)");
	} else {
		current_thinking_toggle->set_text(U"▼ Thinking...");
	}
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

	// Add inline for proper interleaving (no move_child - keeps chronological order)
	current_turn->add_child(current_response_text);

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

PanelContainer *AIChatDock::_create_tool_card(const String &p_id, const String &p_name, const String &p_status) {
	PanelContainer *card = memnew(PanelContainer);
	card->add_theme_style_override("panel", theme_cache.tool_card_bg);

	VBoxContainer *content = memnew(VBoxContainer);
	content->add_theme_constant_override("separation", 4 * EDSCALE);
	card->add_child(content);

	HBoxContainer *row = memnew(HBoxContainer);
	row->add_theme_constant_override("separation", 8 * EDSCALE);
	content->add_child(row);

	// Tool icon (spinner or checkmark)
	Label *icon = memnew(Label);
	icon->set_text(U"◐");
	row->add_child(icon);

	// Tool name + status
	Label *label = memnew(Label);
	label->set_text(p_name + ": " + p_status);
	label->add_theme_font_size_override("font_size", 12 * EDSCALE);
	row->add_child(label);

	Button *details_toggle = memnew(Button);
	details_toggle->set_text(U"▶ Details");
	details_toggle->set_flat(true);
	details_toggle->add_theme_font_size_override("font_size", 10 * EDSCALE);
	details_toggle->add_theme_color_override("font_color", theme_cache.text_muted);
	details_toggle->set_visible(false);
	row->add_child(details_toggle);

	VBoxContainer *details_container = memnew(VBoxContainer);
	details_container->set_visible(false);
	content->add_child(details_container);

	RichTextLabel *details_text = memnew(RichTextLabel);
	details_text->set_autowrap_mode(TextServer::AUTOWRAP_WORD);
	details_text->set_fit_content(true);
	details_text->set_selection_enabled(true);
	details_text->add_theme_font_size_override("normal_font_size", 11 * EDSCALE);
	details_container->add_child(details_text);

	details_toggle->connect("pressed", callable_mp(this, &AIChatDock::_on_tool_details_toggle).bind(details_container, details_toggle));

	active_tool_icons[p_id] = icon;
	active_tool_labels[p_id] = label;
	active_tool_details[p_id] = details_text;
	active_tool_detail_containers[p_id] = details_container;
	active_tool_detail_toggles[p_id] = details_toggle;

	return card;
}

String AIChatDock::_format_tool_status(const String &p_name, const Dictionary &p_input) const {
	if (p_name == "search") {
		String query = p_input.has("query") ? String(p_input["query"]) : String();
		return query.is_empty() ? "searching..." : "searching \"" + query + "\"";
	}
	if (p_name == "read") {
		// Unified read supports string or array target
		if (p_input.has("target")) {
			Variant t = p_input["target"];
			if (t.get_type() == Variant::STRING) {
				String target = t;
				return target.is_empty() ? "reading..." : "reading " + _shorten_path(target);
			} else if (t.get_type() == Variant::ARRAY) {
				Array targets = t;
				return "reading " + itos(targets.size()) + " targets...";
			}
		}
		return "reading...";
	}
	if (p_name == "searchFiles") {
		String query = p_input.has("query") ? String(p_input["query"]) : String();
		return query.is_empty() ? "searching..." : "searching \"" + query + "\"";
	}
	if (p_name == "readFile") {
		String path = p_input.has("path") ? String(p_input["path"]) : String();
		return path.is_empty() ? "reading..." : "reading " + _shorten_path(path);
	}
	if (p_name == "writeFile") {
		String path = p_input.has("path") ? String(p_input["path"]) : String();
		return path.is_empty() ? "writing..." : "writing " + _shorten_path(path);
	}
	if (p_name == "writePatch") {
		return "applying patch...";
	}
	if (p_name == "applySceneEdits") {
		String scene = p_input.has("scenePath") ? String(p_input["scenePath"]) : String();
		return scene.is_empty() ? "editing scene..." : "editing " + _shorten_path(scene);
	}
	if (p_name == "getSceneGraph") {
		String scene = p_input.has("scenePath") ? String(p_input["scenePath"]) : String();
		return scene.is_empty() ? "parsing scene..." : "parsing " + _shorten_path(scene);
	}
	if (p_name == "verify" || p_name == "verifyVisually") {
		String scene = p_input.has("scenePath") ? String(p_input["scenePath"]) : String();
		return scene.is_empty() ? "verifying..." : "verifying " + _shorten_path(scene);
	}
	if (p_name == "runHarness") {
		String scene = p_input.has("scenePath") ? String(p_input["scenePath"]) : String();
		return scene.is_empty() ? "running harness..." : "running harness on " + _shorten_path(scene);
	}
	return "running...";
}

String AIChatDock::_format_tool_progress(const String &p_name, const String &p_stage, const Dictionary &p_input) const {
	if (p_stage.is_empty()) {
		return _format_tool_status(p_name, p_input);
	}
	if (p_name == "search") {
		String query = p_input.has("query") ? String(p_input["query"]) : String();
		// Show which search type is running based on stage
		String search_type;
		if (p_stage.contains("keyword")) {
			search_type = "Keyword search";
		} else if (p_stage.contains("symbol")) {
			search_type = "Index search";
		} else {
			search_type = "Searching";
		}
		return query.is_empty() ? search_type + "..." : search_type + ": \"" + query + "\"";
	}
	if (p_name == "read") {
		// Progress stage often looks like "read:res://path.gd"
		return p_stage;
	}
	if (p_name == "searchFiles") {
		String query = p_input.has("query") ? String(p_input["query"]) : String();
		return query.is_empty() ? p_stage : p_stage + ": \"" + query + "\"";
	}
	if (p_name == "readFile") {
		String path = p_input.has("path") ? String(p_input["path"]) : String();
		return path.is_empty() ? p_stage : p_stage + " " + _shorten_path(path);
	}
	if (p_name == "writeFile") {
		String path = p_input.has("path") ? String(p_input["path"]) : String();
		return path.is_empty() ? p_stage : p_stage + " " + _shorten_path(path);
	}
	if (p_name == "applySceneEdits" || p_name == "getSceneGraph" || p_name == "verify" || p_name == "verifyVisually") {
		String scene = p_input.has("scenePath") ? String(p_input["scenePath"]) : String();
		return scene.is_empty() ? p_stage : p_stage + " " + _shorten_path(scene);
	}
	return p_stage;
}

String AIChatDock::_format_tool_summary(const String &p_name, bool p_ok, const Dictionary &p_output, const Dictionary &p_input) const {
	if (!p_ok) {
		return "failed";
	}
	if (p_name == "search") {
		// Unified search: uses "results" array and "totalMatches"
		Array results = p_output.has("results") ? Array(p_output["results"]) : Array();
		int total = p_output.has("totalMatches") ? int(p_output["totalMatches"]) : results.size();
		Array methods = p_output.has("searchMethods") ? Array(p_output["searchMethods"]) : Array();

		// Format methods nicely: "keyword" → "Keyword", "symbol_index" → "Index"
		String method_str;
		for (int i = 0; i < methods.size(); i++) {
			String m = String(methods[i]);
			if (i > 0) {
				method_str += " + ";
			}
			if (m == "keyword") {
				method_str += "Keyword";
			} else if (m == "symbol_index") {
				method_str += "Index";
			} else {
				method_str += m;
			}
		}
		if (method_str.is_empty()) {
			method_str = "Search";
		}

		return method_str + " (" + itos(total) + (total == 1 ? " match)" : " matches)");
	}
	if (p_name == "read") {
		// Unified read: single or batch result
		String status = p_output.has("status") ? String(p_output["status"]) : String();
		if (status == "in_context") {
			return "in context";
		}
		if (status == "error") {
			return "failed";
		}
		// Single read with content
		if (p_output.has("content")) {
			String content = String(p_output["content"]);
			int lines = p_output.has("linesLoaded") ? int(p_output["linesLoaded"]) : content.split("\n", false).size();
			bool outlined = p_output.has("outlined") ? bool(p_output["outlined"]) : false;
			return outlined ? "done (outline, " + itos(lines) + " lines)" : "done (" + itos(lines) + " lines)";
		}
		// Batch read
		if (p_output.has("results")) {
			Array results = p_output["results"];
			return "done (" + itos(results.size()) + " files)";
		}
		return "done";
	}
	if (p_name == "searchFiles") {
		Array hits = p_output.has("hits") ? Array(p_output["hits"]) : Array();
		return "done (" + itos(hits.size()) + " hits)";
	}
	if (p_name == "readFile") {
		String content = p_output.has("content") ? String(p_output["content"]) : String();
		int lines = content.is_empty() ? 0 : content.split("\n", false).size();
		return "done (" + itos(lines) + " lines)";
	}
	if (p_name == "writeFile") {
		Dictionary res = p_output.has("writeFileResult") ? Dictionary(p_output["writeFileResult"]) : Dictionary();
		String path = res.has("path") ? String(res["path"]) : String();
		return path.is_empty() ? "done" : "done (" + _shorten_path(path) + ")";
	}
	if (p_name == "writePatch") {
		Dictionary res = p_output.has("applyResult") ? Dictionary(p_output["applyResult"]) : Dictionary();
		Dictionary stats = res.has("stats") ? Dictionary(res["stats"]) : Dictionary();
		int files = stats.has("files") ? int(stats["files"]) : 0;
		return files > 0 ? "done (" + itos(files) + " files)" : "done";
	}
	if (p_name == "applySceneEdits") {
		String scene = p_input.has("scenePath") ? String(p_input["scenePath"]) : String();
		return scene.is_empty() ? "done" : "done (" + _shorten_path(scene) + ")";
	}
	if (p_name == "task") {
		String summary = p_output.has("summary") ? String(p_output["summary"]) : String();
		if (summary.is_empty()) {
			return "done";
		}
		return _truncate_text(_collapse_whitespace(summary), 80);
	}
	if (p_name == "verify" || p_name == "verifyVisually" || p_name == "runHarness") {
		Array errors = p_output.has("errors") ? Array(p_output["errors"]) : Array();
		Array warnings = p_output.has("warnings") ? Array(p_output["warnings"]) : Array();
		if (errors.size() > 0) {
			return "done (" + itos(errors.size()) + " errors)";
		}
		if (warnings.size() > 0) {
			return "done (" + itos(warnings.size()) + " warnings)";
		}
		return "done (ok)";
	}
	return "done";
}

String AIChatDock::_format_tool_details(const String &p_name, bool p_ok, const Dictionary &p_output, const Dictionary &p_input) const {
	String details;

	if (p_name == "task") {
		String summary = p_output.has("summary") ? String(p_output["summary"]) : String();
		if (!summary.is_empty()) {
			details += "Summary:\n" + summary.strip_edges() + "\n\n";
		}
		if (p_output.has("toolsUsed")) {
			Array tools = p_output["toolsUsed"];
			if (!tools.is_empty()) {
				details += "Tools used: ";
				for (int i = 0; i < tools.size(); i++) {
					if (i > 0) {
						details += ", ";
					}
					details += String(tools[i]);
				}
				details += "\n";
			}
		}
		if (p_output.has("message") && p_output["message"].get_type() == Variant::DICTIONARY) {
			const String stream = _format_task_stream(Dictionary(p_output["message"]));
			if (!stream.is_empty()) {
				details += "\nStream:\n" + stream + "\n";
			}
		}
		return details.strip_edges();
	}

	// Unified search tool (primary)
	if (p_name == "search") {
		String query = p_input.has("query") ? String(p_input["query"]) : String();
		String scope = p_input.has("scope") ? String(p_input["scope"]) : String();
		if (!query.is_empty()) {
			details += "Query: " + query + "\n";
		}
		if (!scope.is_empty()) {
			details += "Scope: " + scope + "\n";
		}
		Array results = p_output.has("results") ? Array(p_output["results"]) : Array();
		int total = p_output.has("totalMatches") ? int(p_output["totalMatches"]) : results.size();
		Array methods = p_output.has("searchMethods") ? Array(p_output["searchMethods"]) : Array();
		String methods_str;
		for (int i = 0; i < methods.size(); i++) {
			if (i > 0) {
				methods_str += ", ";
			}
			methods_str += String(methods[i]);
		}
		details += "Matches: " + itos(total);
		if (!methods_str.is_empty()) {
			details += " (via " + methods_str + ")";
		}
		details += "\n";
		int limit = results.size() < 10 ? results.size() : 10;
		for (int i = 0; i < limit; i++) {
			Dictionary hit = results[i];
			String path = hit.has("path") ? String(hit["path"]) : String();
			int line = hit.has("line") ? int(hit["line"]) : 0;
			String preview = hit.has("preview") ? String(hit["preview"]) : String();
			String symbol = hit.has("symbol") ? String(hit["symbol"]) : String();
			String source = hit.has("source") ? String(hit["source"]) : String();
			if (!path.is_empty()) {
				path = _shorten_path(path);
			}
			details += "- " + (path.is_empty() ? String("(unknown)") : path);
			if (line > 0) {
				details += ":" + itos(line);
			}
			if (!symbol.is_empty()) {
				details += " [" + symbol + "]";
			}
			if (!preview.is_empty()) {
				details += "  " + preview;
			}
			details += "\n";
		}
		if (results.size() > limit) {
			details += "... +" + itos(results.size() - limit) + " more\n";
		}
		return details.strip_edges();
	}

	// Unified read tool
	if (p_name == "read") {
		String status = p_output.has("status") ? String(p_output["status"]) : String();
		String target_str;
		if (p_input.has("target")) {
			Variant t = p_input["target"];
			if (t.get_type() == Variant::STRING) {
				target_str = _shorten_path(t);
			} else if (t.get_type() == Variant::ARRAY) {
				Array targets = t;
				for (int i = 0; i < targets.size() && i < 5; i++) {
					if (i > 0) {
						target_str += ", ";
					}
					target_str += _shorten_path(String(targets[i]));
				}
				if (targets.size() > 5) {
					target_str += "... +" + itos(targets.size() - 5) + " more";
				}
			}
		}
		if (!target_str.is_empty()) {
			details += "Target: " + target_str + "\n";
		}
		// Handle in_context status
		if (status == "in_context") {
			String msg = p_output.has("message") ? String(p_output["message"]) : String("Already in context");
			details += msg;
			return details.strip_edges();
		}
		// Handle error status
		if (status == "error") {
			String msg = p_output.has("message") ? String(p_output["message"]) : String("Read failed");
			details += "Error: " + msg;
			if (p_output.has("available")) {
				Array available = p_output["available"];
				if (!available.is_empty()) {
					details += "\nAvailable symbols:\n";
					for (int i = 0; i < available.size() && i < 10; i++) {
						details += "- " + String(available[i]) + "\n";
					}
				}
			}
			return details.strip_edges();
		}
		// Handle single read with content
		if (p_output.has("content")) {
			String target = p_output.has("target") ? _shorten_path(String(p_output["target"])) : target_str;
			String content = String(p_output["content"]);
			int lines = p_output.has("linesLoaded") ? int(p_output["linesLoaded"]) : content.split("\n", false).size();
			bool outlined = p_output.has("outlined") ? bool(p_output["outlined"]) : false;
			if (!target.is_empty()) {
				details += "Path: " + target + "\n";
			}
			details += "Lines: " + itos(lines);
			if (outlined) {
				details += " (outlined)";
			}
			details += "\n";
			if (p_output.has("symbols")) {
				Array symbols = p_output["symbols"];
				if (!symbols.is_empty()) {
					details += "Symbols: ";
					for (int i = 0; i < symbols.size() && i < 8; i++) {
						if (i > 0) {
							details += ", ";
						}
						details += String(symbols[i]);
					}
					if (symbols.size() > 8) {
						details += "... +" + itos(symbols.size() - 8);
					}
					details += "\n";
				}
			}
			// Show content snippet
			if (!content.is_empty()) {
				const int max_chars = 1600;
				String snippet = content;
				bool truncated = false;
				if (snippet.length() > max_chars) {
					snippet = snippet.substr(0, max_chars);
					truncated = true;
				}
				details += "\n" + snippet;
				if (truncated) {
					details += "\n... (truncated)";
				}
			}
			return details.strip_edges();
		}
		// Handle batch results
		if (p_output.has("results")) {
			Array results = p_output["results"];
			details += "Read " + itos(results.size()) + " files:\n";
			for (int i = 0; i < results.size() && i < 10; i++) {
				Dictionary res = results[i];
				String tgt = res.has("target") ? _shorten_path(String(res["target"])) : String();
				String st = res.has("status") ? String(res["status"]) : String("ok");
				int lns = res.has("linesLoaded") ? int(res["linesLoaded"]) : 0;
				details += "- " + (tgt.is_empty() ? String("(unknown)") : tgt);
				if (st == "in_context") {
					details += " [in context]";
				} else if (st == "error") {
					details += " [error]";
				} else if (lns > 0) {
					details += " (" + itos(lns) + " lines)";
				}
				details += "\n";
			}
			if (results.size() > 10) {
				details += "... +" + itos(results.size() - 10) + " more\n";
			}
			return details.strip_edges();
		}
		return details.strip_edges();
	}

	// Legacy searchFiles tool
	if (p_name == "searchFiles") {
		String query = p_input.has("query") ? String(p_input["query"]) : String();
		if (!query.is_empty()) {
			details += "Query: " + query + "\n";
		}
		Array hits = p_output.has("hits") ? Array(p_output["hits"]) : Array();
		details += "Hits: " + itos(hits.size()) + "\n";
		int limit = hits.size() < 10 ? hits.size() : 10;
		for (int i = 0; i < limit; i++) {
			Dictionary hit = hits[i];
			String path = hit.has("path") ? String(hit["path"]) : String();
			int line = hit.has("line") ? int(hit["line"]) : 0;
			String preview = hit.has("preview") ? String(hit["preview"]) : String();
			if (!path.is_empty()) {
				path = _shorten_path(path);
			}
			details += "- " + (path.is_empty() ? String("(unknown)") : path);
			if (line > 0) {
				details += ":" + itos(line);
			}
			if (!preview.is_empty()) {
				details += "  " + preview;
			}
			details += "\n";
		}
		if (hits.size() > limit) {
			details += "... +" + itos(hits.size() - limit) + " more\n";
		}
		return details.strip_edges();
	}

	if (p_name == "readFile") {
		String path = p_output.has("path") ? String(p_output["path"]) : String();
		String content = p_output.has("content") ? String(p_output["content"]) : String();
		if (!path.is_empty()) {
			path = _shorten_path(path);
		}
		int lines = content.is_empty() ? 0 : content.split("\n", false).size();
		if (!path.is_empty()) {
			details += "Path: " + path + "\n";
		}
		details += "Lines: " + itos(lines) + " | Chars: " + itos(content.length()) + "\n";
		if (!content.is_empty()) {
			const int max_chars = 1600;
			String snippet = content;
			bool truncated = false;
			if (snippet.length() > max_chars) {
				snippet = snippet.substr(0, max_chars);
				truncated = true;
			}
			details += "\n" + snippet;
			if (truncated) {
				details += "\n... (truncated)";
			}
		}
		return details.strip_edges();
	}

	if (p_name == "writeFile") {
		if (p_output.has("writeFileResult")) {
			Dictionary res = p_output["writeFileResult"];
			bool ok = res.has("ok") ? bool(res["ok"]) : p_ok;
			String path = res.has("path") ? String(res["path"]) : String();
			if (!path.is_empty()) {
				path = _shorten_path(path);
			}
			details += ok ? "Wrote: " : "Write failed: ";
			details += path.is_empty() ? String("(unknown)") : path;
			if (res.has("bytes")) {
				details += " (" + itos(int(res["bytes"])) + " bytes)";
			}
			if (!ok && res.has("error")) {
				details += "\n" + String(res["error"]);
			}
			return details.strip_edges();
		}
		if (p_output.has("preview")) {
			Dictionary preview = p_output["preview"];
			String path = preview.has("path") ? String(preview["path"]) : String();
			if (!path.is_empty()) {
				path = _shorten_path(path);
			}
			details = "Preview: " + (path.is_empty() ? String("(unknown)") : path);
			if (preview.has("bytes")) {
				details += " (" + itos(int(preview["bytes"])) + " bytes)";
			}
			return details.strip_edges();
		}
	}

	if (p_name == "writePatch") {
		if (p_output.has("applyResult")) {
			Dictionary res = p_output["applyResult"];
			Dictionary stats = res.has("stats") ? Dictionary(res["stats"]) : Dictionary();
			int files = stats.has("files") ? int(stats["files"]) : 0;
			int hunks = stats.has("hunks") ? int(stats["hunks"]) : 0;
			int applied = stats.has("applied") ? int(stats["applied"]) : 0;
			int skipped = stats.has("skipped") ? int(stats["skipped"]) : 0;
			details += "Files: " + itos(files) + " | Hunks: " + itos(hunks) + " | Applied: " + itos(applied) + " | Skipped: " + itos(skipped) + "\n";
			Array results = res.has("results") ? Array(res["results"]) : Array();
			if (!results.is_empty()) {
				details += "Files:\n";
				for (int i = 0; i < results.size(); i++) {
					Dictionary file_res = results[i];
					String path = file_res.has("path") ? String(file_res["path"]) : String();
					bool wrote = file_res.has("wrote") ? bool(file_res["wrote"]) : false;
					if (!path.is_empty()) {
						path = _shorten_path(path);
					}
					details += "- " + (path.is_empty() ? String("(unknown)") : path);
					if (!wrote) {
						details += " (no changes)";
					}
					details += "\n";
				}
			}
			return details.strip_edges();
		}
		if (p_output.has("previewDiff")) {
			Dictionary preview = p_output["previewDiff"];
			Array files = preview.has("files") ? Array(preview["files"]) : Array();
			details += "Preview files: " + itos(files.size()) + "\n";
			int limit = files.size() < 5 ? files.size() : 5;
			for (int i = 0; i < limit; i++) {
				Dictionary file = files[i];
				String path = file.has("path") ? String(file["path"]) : String();
				if (!path.is_empty()) {
					path = _shorten_path(path);
				}
				details += "- " + (path.is_empty() ? String("(unknown)") : path) + "\n";
			}
			if (files.size() > limit) {
				details += "... +" + itos(files.size() - limit) + " more\n";
			}
			return details.strip_edges();
		}
	}

	if (p_name == "applySceneEdits") {
		Dictionary res = p_output.has("sceneEditResult") ? Dictionary(p_output["sceneEditResult"]) : Dictionary();
		bool ok = res.has("ok") ? bool(res["ok"]) : p_ok;
		String scene = res.has("scenePath") ? String(res["scenePath"]) : String();
		if (scene.is_empty() && p_input.has("scenePath")) {
			scene = String(p_input["scenePath"]);
		}
		if (!scene.is_empty()) {
			scene = _shorten_path(scene);
			details += "Scene: " + scene + "\n";
		}
		details += ok ? "Applied edits." : "Edit failed.";
		if (p_input.has("edits")) {
			Array edits = p_input["edits"];
			details += "\nEdits: " + itos(edits.size());
		}
		return details.strip_edges();
	}

	if (p_name == "getSceneGraph") {
		String scene = p_input.has("scenePath") ? String(p_input["scenePath"]) : String();
		return scene.is_empty() ? String() : ("Parsed scene: " + _shorten_path(scene));
	}

	if (p_name == "verify" || p_name == "verifyVisually" || p_name == "runHarness") {
		Dictionary vr = p_output.has("verifyResult") ? Dictionary(p_output["verifyResult"]) : Dictionary();
		String kind = vr.has("kind") ? String(vr["kind"]) : (p_output.has("kind") ? String(p_output["kind"]) : String());
		Array errors = vr.has("errors") ? Array(vr["errors"]) : (p_output.has("errors") ? Array(p_output["errors"]) : Array());
		Array warnings = vr.has("warnings") ? Array(vr["warnings"]) : (p_output.has("warnings") ? Array(p_output["warnings"]) : Array());
		Array logs = vr.has("logs") ? Array(vr["logs"]) : (p_output.has("logs") ? Array(p_output["logs"]) : Array());
		String scene = vr.has("scenePath") ? String(vr["scenePath"]) : (p_input.has("scenePath") ? String(p_input["scenePath"]) : String());
		int frames = vr.has("frames") ? int(vr["frames"]) : (p_output.has("frames") ? int(p_output["frames"]) : 0);

		// Mode indicator
		if (kind == "harness") {
			details += "Mode: headless runtime";
			if (frames > 0) {
				details += " (" + itos(frames) + " frames)";
			}
			details += "\n";
		} else if (kind == "diagnostics") {
			details += "Mode: static analysis\n";
		}

		if (!scene.is_empty()) {
			details += "Scene: " + _shorten_path(scene) + "\n";
		}
		if (vr.has("screenshot")) {
			details += "Screenshot: " + String(vr["screenshot"]) + "\n";
		}

		// Errors
		if (!errors.is_empty()) {
			details += "Errors (" + itos(errors.size()) + "):\n";
			int limit = errors.size() < 5 ? errors.size() : 5;
			for (int i = 0; i < limit; i++) {
				details += "  - " + String(errors[i]) + "\n";
			}
			if (errors.size() > limit) {
				details += "  ... +" + itos(errors.size() - limit) + " more\n";
			}
		}

		// Warnings
		if (!warnings.is_empty()) {
			details += "Warnings (" + itos(warnings.size()) + "):\n";
			int limit = warnings.size() < 5 ? warnings.size() : 5;
			for (int i = 0; i < limit; i++) {
				details += "  - " + String(warnings[i]) + "\n";
			}
			if (warnings.size() > limit) {
				details += "  ... +" + itos(warnings.size() - limit) + " more\n";
			}
		}

		// Logs (harness mode stdout/stderr including print() statements)
		if (!logs.is_empty()) {
			// Filter out internal harness meta-lines for cleaner display
			Array user_logs;
			for (int i = 0; i < logs.size(); i++) {
				String line = String(logs[i]);
				// Skip harness spawn info and exit codes
				if (line.begins_with("spawn:") || line.begins_with("scene:") ||
						line.begins_with("frames:") || line.begins_with("exit:") ||
						line.begins_with("timeout")) {
					continue;
				}
				user_logs.push_back(line);
			}
			if (!user_logs.is_empty()) {
				details += "Output (" + itos(user_logs.size()) + " lines):\n";
				int limit = user_logs.size() < 8 ? user_logs.size() : 8;
				for (int i = 0; i < limit; i++) {
					details += "  " + String(user_logs[i]) + "\n";
				}
				if (user_logs.size() > limit) {
					details += "  ... +" + itos(user_logs.size() - limit) + " more\n";
				}
			}
		}

		// Success summary if no issues
		if (errors.is_empty() && warnings.is_empty()) {
			if (kind == "harness") {
				details += "No runtime errors.";
			} else if (kind == "diagnostics") {
				details += "No static errors.";
			} else {
				details += "No issues found.";
			}
		}
		return details.strip_edges();
	}

	if (!p_ok) {
		return "Tool failed.";
	}
	return String();
}

void AIChatDock::_on_tool_details_toggle(Control *p_container, Button *p_toggle) {
	if (!p_container || !p_toggle) {
		return;
	}
	const bool show = !p_container->is_visible();
	p_container->set_visible(show);
	p_toggle->set_text(show ? U"▼ Details" : U"▶ Details");
}

void AIChatDock::_update_spinner_icons() {
	if (active_tool_icons.is_empty()) {
		return;
	}
	uint64_t now = OS::get_singleton()->get_ticks_msec();
	if (now - last_spinner_msec < 120) {
		return;
	}
	last_spinner_msec = now;
	spinner_frame = (spinner_frame + 1) % 4;
	static const String frames[] = { U"◐", U"◓", U"◑", U"◒" };
	const String frame = frames[spinner_frame];
	for (const KeyValue<String, Label *> &E : active_tool_icons) {
		if (E.value) {
			E.value->set_text(frame);
		}
	}
}

// === Event handlers ===

void AIChatDock::_on_send_pressed() {
	String text = input->get_text().strip_edges();
	if (text.is_empty() && pending_images.is_empty()) {
		return;
	}

	// End any previous turn
	end_turn();

	// Add mentioned items to context before sending
	if (EditorAIAgent *agent = EditorAIAgent::get_singleton()) {
		for (const String &path : mentioned_paths) {
			if (path.begins_with("node:")) {
				// Scene tree node
				String node_path = path.substr(5);
				agent->add_context_item(AI_CONTEXT_NODE, node_path, node_path.get_file());
			} else {
				// File path
				AIContextItemKind kind = ai_context_kind_from_path(path);
				agent->add_context_item(kind, path, path.get_file());
			}
		}
	}

	// Add user message
	// Sending is explicit intent to follow the conversation; re-enable sticky autoscroll.
	stick_to_bottom = true;
	append_user_message(text);

	// Send to agent
	if (EditorAIAgent *agent = EditorAIAgent::get_singleton()) {
		Array images;
		for (const PendingImage &img : pending_images) {
			Dictionary payload;
			payload["data"] = img.data_base64;
			payload["mime"] = img.mime;
			if (!img.label.is_empty()) {
				payload["label"] = img.label;
			}
			images.push_back(payload);
		}
		agent->request_chat(text, images);
		agent->clear_context();
	}

	input->clear();
	_clear_context_chips();
	_clear_pinned_chips();
	_clear_image_attachments();
}

void AIChatDock::_on_stop_pressed() {
	if (EditorAIAgent *agent = EditorAIAgent::get_singleton()) {
		agent->abort_chat();
	}
}

void AIChatDock::_on_processing_state_changed(bool p_is_processing) {
	if (send_btn) {
		send_btn->set_visible(!p_is_processing);
	}
	if (stop_btn) {
		stop_btn->set_visible(p_is_processing);
	}
}

void AIChatDock::_on_input_gui_input(const Ref<InputEvent> &p_event) {
	Ref<InputEventKey> key = p_event;
	if (key.is_valid() && key->is_pressed() && !key->is_echo()) {
		if (key->get_keycode() == Key::V && key->is_command_or_control_pressed()) {
			if (_try_attach_clipboard_image()) {
				input->accept_event();
				return;
			}
		}

		// Forward navigation keys to mention popup if visible
		if (mention_popup && mention_popup->is_visible()) {
			Key keycode = key->get_keycode();
			if (keycode == Key::UP || keycode == Key::DOWN ||
					keycode == Key::ESCAPE || keycode == Key::TAB ||
					(keycode == Key::ENTER && !key->is_shift_pressed())) {
				if (mention_popup->handle_key(keycode)) {
					input->accept_event();
					return;
				}
			}
		}

		// Normal Enter to send (when popup not handling it)
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

// === @ Mention handlers ===

void AIChatDock::_on_input_text_changed() {
	if (!input || !mention_popup) {
		return;
	}

	String text = input->get_text();
	int caret_col = input->get_caret_column();
	int caret_line = input->get_caret_line();

	// Get the current line text up to caret
	PackedStringArray lines = text.split("\n");
	if (caret_line >= lines.size()) {
		mention_popup->cancel();
		return;
	}
	String line = lines[caret_line];
	String before_caret = line.substr(0, caret_col);

	// Delegate to popup (now a Control, not a Window, so no focus stealing)
	if (mention_popup->update_filter(before_caret, caret_col)) {
		mention_popup->show_at(input_container);
	}
}

void AIChatDock::_on_mention_selected(const String &p_path, const String &p_label, int p_start_col) {
	if (!input) {
		return;
	}

	// Track the mentioned path
	if (!mentioned_paths.has(p_path)) {
		mentioned_paths.push_back(p_path);
		_add_context_chip(p_path, p_label);
	}

	// Replace @filter with @label in input
	String text = input->get_text();
	int caret_line = input->get_caret_line();
	PackedStringArray lines = text.split("\n");

	if (caret_line < lines.size()) {
		String line = lines[caret_line];
		int caret_col = input->get_caret_column();

		// Replace from @ to caret with @label + space
		String new_line = line.substr(0, p_start_col) + "@" + p_label + " ";
		if (caret_col < line.length()) {
			new_line += line.substr(caret_col);
		}
		lines.set(caret_line, new_line);

		// Rebuild text
		String new_text;
		for (int i = 0; i < lines.size(); i++) {
			if (i > 0) {
				new_text += "\n";
			}
			new_text += lines[i];
		}
		input->set_text(new_text);

		// Move caret after inserted mention
		int new_col = p_start_col + 1 + p_label.length() + 1;
		input->set_caret_column(new_col);
		input->set_caret_line(caret_line);
	}
}

void AIChatDock::_on_capture_pressed() {
	String label;
	Ref<Image> image = _capture_viewport_image(label);
	if (!image.is_valid()) {
		return;
	}
	_add_image_attachment(image, label);
}

bool AIChatDock::_try_attach_clipboard_image() {
	DisplayServer *display = DisplayServer::get_singleton();
	if (!display || !display->clipboard_has_image()) {
		return false;
	}

	Ref<Image> image = display->clipboard_get_image();
	if (!image.is_valid() || image->get_width() <= 0 || image->get_height() <= 0) {
		return false;
	}

	_add_image_attachment(image, "Clipboard image");
	return true;
}

void AIChatDock::_add_image_attachment(const Ref<Image> &p_image, const String &p_label) {
	if (!p_image.is_valid()) {
		return;
	}

	Ref<Image> image = p_image->duplicate();
	if (!image.is_valid() || image->get_width() <= 0 || image->get_height() <= 0) {
		return;
	}

	Vector<uint8_t> buffer = image->save_png_to_buffer();
	if (buffer.is_empty()) {
		return;
	}

	PendingImage pending;
	pending.id = String::num_uint64(++image_sequence);
	pending.label = p_label.is_empty() ? String("Image") : p_label;
	pending.mime = "image/png";
	if (Marshalls *marshalls = Marshalls::get_singleton()) {
		pending.data_base64 = marshalls->raw_to_base64(buffer);
	} else {
		return;
	}

	Ref<Image> thumb = image->duplicate();
	int max_size = int(96 * EDSCALE);
	if (max_size < 1) {
		max_size = 1;
	}
	int w = thumb->get_width();
	int h = thumb->get_height();
	if (w > max_size || h > max_size) {
		float scale = MIN(float(max_size) / w, float(max_size) / h);
		thumb->resize(MAX(1, int(w * scale)), MAX(1, int(h * scale)), Image::INTERPOLATE_LANCZOS);
	}
	pending.preview = ImageTexture::create_from_image(thumb);

	pending_images.push_back(pending);
	_add_image_chip(pending);
}

Ref<Image> AIChatDock::_capture_viewport_image(String &r_label) const {
	EditorNode *editor = EditorNode::get_singleton();
	if (!editor) {
		return Ref<Image>();
	}

	auto capture_2d = [&]() -> Ref<Image> {
		SubViewport *scene_root = editor->get_scene_root();
		if (!scene_root) {
			return Ref<Image>();
		}
		Ref<ViewportTexture> texture = scene_root->get_texture();
		if (!texture.is_valid() || texture->get_width() <= 0 || texture->get_height() <= 0) {
			return Ref<Image>();
		}
		return texture->get_image();
	};

	auto capture_3d = [&]() -> Ref<Image> {
		Node3DEditor *editor_3d = Node3DEditor::get_singleton();
		if (!editor_3d) {
			return Ref<Image>();
		}
		Node3DEditorViewport *viewport = editor_3d->get_editor_viewport(0);
		if (!viewport) {
			return Ref<Image>();
		}
		Viewport *vp_node = viewport->get_viewport_node();
		if (!vp_node) {
			return Ref<Image>();
		}
		Ref<ViewportTexture> texture = vp_node->get_texture();
		if (!texture.is_valid() || texture->get_width() <= 0 || texture->get_height() <= 0) {
			return Ref<Image>();
		}
		return texture->get_image();
	};

	Ref<Image> image;
	int selected = -1;
	if (EditorMainScreen *main_screen = editor->get_editor_main_screen()) {
		selected = main_screen->get_selected_index();
	}

	if (selected == EditorMainScreen::EDITOR_2D) {
		image = capture_2d();
		if (image.is_valid()) {
			r_label = "Viewport 2D";
		}
	} else if (selected == EditorMainScreen::EDITOR_3D) {
		image = capture_3d();
		if (image.is_valid()) {
			r_label = "Viewport 3D";
		}
	}

	if (!image.is_valid()) {
		int c2d = 0;
		int c3d = 0;
		if (Node *root = editor->get_edited_scene()) {
			_count_scene_node_types(root, root, c2d, c3d);
		}

		if (c3d >= c2d) {
			image = capture_3d();
			if (image.is_valid()) {
				r_label = "Viewport 3D";
			}
		}
		if (!image.is_valid()) {
			image = capture_2d();
			if (image.is_valid()) {
				r_label = "Viewport 2D";
			}
		}
	}

	if (image.is_valid()) {
		return image->duplicate();
	}

	return image;
}

// === Context chip helpers ===

void AIChatDock::_add_image_chip(const PendingImage &p_image) {
	if (!attachment_chips) {
		return;
	}

	HBoxContainer *chip = memnew(HBoxContainer);
	chip->add_theme_constant_override("separation", 4 * EDSCALE);
	chip->set_meta("image_id", p_image.id);

	PanelContainer *chip_panel = memnew(PanelContainer);
	Ref<StyleBoxFlat> chip_style;
	chip_style.instantiate();
	chip_style->set_bg_color(theme_cache.accent_color.lerp(Color(0.2, 0.2, 0.2), 0.75));
	chip_style->set_corner_radius_all(10 * EDSCALE);
	chip_style->set_content_margin(SIDE_LEFT, 6 * EDSCALE);
	chip_style->set_content_margin(SIDE_RIGHT, 4 * EDSCALE);
	chip_style->set_content_margin(SIDE_TOP, 4 * EDSCALE);
	chip_style->set_content_margin(SIDE_BOTTOM, 4 * EDSCALE);
	chip_panel->add_theme_style_override("panel", chip_style);

	HBoxContainer *chip_content = memnew(HBoxContainer);
	chip_content->add_theme_constant_override("separation", 6 * EDSCALE);
	chip_panel->add_child(chip_content);

	if (p_image.preview.is_valid()) {
		TextureRect *thumb = memnew(TextureRect);
		thumb->set_texture(p_image.preview);
		thumb->set_custom_minimum_size(Size2(48, 48) * EDSCALE);
		thumb->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
		chip_content->add_child(thumb);
	}

	Label *label = memnew(Label);
	label->set_text(p_image.label);
	label->add_theme_font_size_override("font_size", 10 * EDSCALE);
	chip_content->add_child(label);

	Button *remove_btn = memnew(Button);
	remove_btn->set_text(U"×");
	remove_btn->set_flat(true);
	remove_btn->add_theme_font_size_override("font_size", 12 * EDSCALE);
	remove_btn->connect("pressed", callable_mp(this, &AIChatDock::_remove_image_attachment).bind(p_image.id));
	chip_content->add_child(remove_btn);

	chip->add_child(chip_panel);
	attachment_chips->add_child(chip);
	attachment_chips->set_visible(true);
}

void AIChatDock::_add_context_chip(const String &p_path, const String &p_label) {
	if (!context_chips) {
		return;
	}

	// Create chip button with x to remove
	HBoxContainer *chip = memnew(HBoxContainer);
	chip->add_theme_constant_override("separation", 4 * EDSCALE);
	chip->set_meta("path", p_path);

	// Apply chip style
	PanelContainer *chip_panel = memnew(PanelContainer);
	Ref<StyleBoxFlat> chip_style;
	chip_style.instantiate();
	chip_style->set_bg_color(theme_cache.accent_color.lerp(Color(0.2, 0.2, 0.2), 0.7));
	chip_style->set_corner_radius_all(12 * EDSCALE);
	chip_style->set_content_margin(SIDE_LEFT, 8 * EDSCALE);
	chip_style->set_content_margin(SIDE_RIGHT, 4 * EDSCALE);
	chip_style->set_content_margin(SIDE_TOP, 2 * EDSCALE);
	chip_style->set_content_margin(SIDE_BOTTOM, 2 * EDSCALE);
	chip_panel->add_theme_style_override("panel", chip_style);

	HBoxContainer *chip_content = memnew(HBoxContainer);
	chip_content->add_theme_constant_override("separation", 4 * EDSCALE);
	chip_panel->add_child(chip_content);

	Label *label = memnew(Label);
	label->set_text(p_label);
	label->add_theme_font_size_override("font_size", 11 * EDSCALE);
	chip_content->add_child(label);

	Button *remove_btn = memnew(Button);
	remove_btn->set_text(U"×");
	remove_btn->set_flat(true);
	remove_btn->add_theme_font_size_override("font_size", 12 * EDSCALE);
	remove_btn->connect("pressed", callable_mp(this, &AIChatDock::_remove_context_chip).bind(p_path));
	chip_content->add_child(remove_btn);

	chip->add_child(chip_panel);
	context_chips->add_child(chip);
	context_chips->set_visible(true);
}

void AIChatDock::_remove_context_chip(const String &p_path) {
	if (!context_chips) {
		return;
	}

	// Remove from tracked paths
	mentioned_paths.erase(p_path);

	// Remove chip from UI
	for (int i = context_chips->get_child_count() - 1; i >= 0; i--) {
		Node *child = context_chips->get_child(i);
		if (child->has_meta("path") && String(child->get_meta("path")) == p_path) {
			child->queue_free();
			break;
		}
	}

	// Hide container if empty
	if (context_chips->get_child_count() <= 1) {
		context_chips->set_visible(false);
	}
}

void AIChatDock::_clear_context_chips() {
	if (!context_chips) {
		return;
	}

	for (int i = context_chips->get_child_count() - 1; i >= 0; i--) {
		context_chips->get_child(i)->queue_free();
	}
	context_chips->set_visible(false);
	mentioned_paths.clear();
}

void AIChatDock::_remove_image_attachment(const String &p_id) {
	for (int i = 0; i < pending_images.size(); i++) {
		if (pending_images[i].id == p_id) {
			pending_images.remove_at(i);
			break;
		}
	}

	if (!attachment_chips) {
		return;
	}

	for (int i = attachment_chips->get_child_count() - 1; i >= 0; i--) {
		Node *child = attachment_chips->get_child(i);
		if (child->has_meta("image_id") && String(child->get_meta("image_id")) == p_id) {
			child->queue_free();
			break;
		}
	}

	if (attachment_chips->get_child_count() == 0) {
		attachment_chips->set_visible(false);
	}
}

void AIChatDock::_clear_image_attachments() {
	pending_images.clear();
	if (!attachment_chips) {
		return;
	}
	for (int i = attachment_chips->get_child_count() - 1; i >= 0; i--) {
		attachment_chips->get_child(i)->queue_free();
	}
	attachment_chips->set_visible(false);
}

void AIChatDock::_add_pinned_chip(const String &p_item_id, const String &p_label) {
	if (!pinned_chips) {
		return;
	}

	HBoxContainer *chip = memnew(HBoxContainer);
	chip->add_theme_constant_override("separation", 4 * EDSCALE);
	chip->set_meta("item_id", p_item_id);

	PanelContainer *chip_panel = memnew(PanelContainer);
	Ref<StyleBoxFlat> chip_style;
	chip_style.instantiate();
	chip_style->set_bg_color(theme_cache.accent_color.lerp(Color(0.2, 0.2, 0.2), 0.75));
	chip_style->set_corner_radius_all(12 * EDSCALE);
	chip_style->set_content_margin(SIDE_LEFT, 8 * EDSCALE);
	chip_style->set_content_margin(SIDE_RIGHT, 4 * EDSCALE);
	chip_style->set_content_margin(SIDE_TOP, 2 * EDSCALE);
	chip_style->set_content_margin(SIDE_BOTTOM, 2 * EDSCALE);
	chip_panel->add_theme_style_override("panel", chip_style);

	HBoxContainer *chip_content = memnew(HBoxContainer);
	chip_content->add_theme_constant_override("separation", 4 * EDSCALE);
	chip_panel->add_child(chip_content);

	Label *label = memnew(Label);
	label->set_text(p_label);
	label->add_theme_font_size_override("font_size", 11 * EDSCALE);
	chip_content->add_child(label);

	Button *remove_btn = memnew(Button);
	remove_btn->set_text(U"×");
	remove_btn->set_flat(true);
	remove_btn->add_theme_font_size_override("font_size", 12 * EDSCALE);
	remove_btn->connect("pressed", callable_mp(this, &AIChatDock::_remove_pinned_chip).bind(p_item_id));
	chip_content->add_child(remove_btn);

	chip->add_child(chip_panel);
	pinned_chips->add_child(chip);
	pinned_chips->set_visible(true);
}

void AIChatDock::_clear_pinned_chips() {
	if (!pinned_chips) {
		return;
	}

	for (int i = pinned_chips->get_child_count() - 1; i >= 0; i--) {
		pinned_chips->get_child(i)->queue_free();
	}
	pinned_chips->set_visible(false);
}

void AIChatDock::_remove_pinned_chip(const String &p_item_id) {
	if (EditorAIAgent *agent = EditorAIAgent::get_singleton()) {
		agent->remove_context_item(p_item_id);
	}
}

void AIChatDock::_refresh_pinned_chips(const Array &p_items) {
	if (!pinned_chips) {
		return;
	}

	_clear_pinned_chips();

	for (int i = 0; i < p_items.size(); i++) {
		if (p_items[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		Dictionary dict = p_items[i];
		AIContextItem item = AIContextItem::from_dict(dict);
		String label = item.label.is_empty() ? item.path.get_file() : item.label;
		if (item.kind == AI_CONTEXT_LOG && label.is_empty()) {
			label = "Runtime log";
		}
		if (item.id.is_empty()) {
			continue;
		}
		_add_pinned_chip(item.id, label);
	}
}

// === Agent signal handlers ===

void AIChatDock::_on_thinking(const String &p_text) {
	append_thinking(p_text);
}

void AIChatDock::_on_status(const String &p_level, const String &p_message) {
	if (p_message == "chat:done") {
		end_turn();
		return;
	}
	// Show skill loading as thinking text
	if (p_message.begins_with("Loading internal skill:")) {
		append_thinking(p_message + "\n");
	}
}

void AIChatDock::_on_usage_updated(int64_t p_turn, int64_t p_session, int p_cache_rate) {
	update_usage(p_turn, p_session, p_cache_rate);
}

void AIChatDock::_on_tool_call(const String &p_id, const String &p_name, const Dictionary &p_input) {
	tool_call_count++;

	// Close current blocks so post-tool thinking/response creates new ones
	// This creates the interleaved flow: thinking → tool → thinking → tool → response
	current_thinking_block = nullptr;
	current_thinking_toggle = nullptr;
	current_thinking_text = nullptr;
	current_response_text = nullptr;
	// Response is rendered by re-converting the full accumulated markdown buffer into BBCode.
	// When we split response blocks around tool calls, we MUST reset the buffer,
	// otherwise the next block re-renders prior text and looks duplicated/"combined".
	current_response_buffer = "";

	const String status = _format_tool_status(p_name, p_input);
	add_tool_card(p_id, p_name, p_input, status);
	_update_meter();
}

void AIChatDock::_on_tool_result(const String &p_id, bool p_ok, const Dictionary &p_output, bool p_preliminary) {
	const String name = active_tool_names.has(p_id) ? active_tool_names[p_id] : String();
	const Dictionary input = active_tool_inputs.has(p_id) ? active_tool_inputs[p_id] : Dictionary();
	const String summary = _format_tool_summary(name, p_ok, p_output, input);
	const String status =
			summary.is_empty() ? (p_ok ? (p_preliminary ? "streaming..." : "done") : "failed") : summary;
	update_tool_card(p_id, status, !p_preliminary);

	const String details = _format_tool_details(name, p_ok, p_output, input);
	if (!details.is_empty()) {
		if (active_tool_details.has(p_id)) {
			if (RichTextLabel *detail_text = active_tool_details[p_id]) {
				detail_text->clear();
				detail_text->append_text(details);
			}
		}
		if (active_tool_detail_containers.has(p_id)) {
			if (Control *container = active_tool_detail_containers[p_id]) {
				bool open = container->is_visible();
				if (p_preliminary && name == "task") {
					open = true;
				}
				container->set_visible(open);
			}
		}
		if (active_tool_detail_toggles.has(p_id)) {
			if (Button *toggle = active_tool_detail_toggles[p_id]) {
				toggle->set_visible(true);
				const bool is_open = active_tool_detail_containers.has(p_id) &&
						active_tool_detail_containers[p_id] &&
						active_tool_detail_containers[p_id]->is_visible();
				toggle->set_text(is_open ? U"▼ Details" : U"▶ Details");
			}
		}
	}
}

void AIChatDock::_on_tool_progress(const String &p_id, const String &p_stage, float p_progress) {
	const String name = active_tool_names.has(p_id) ? active_tool_names[p_id] : String();
	const Dictionary input = active_tool_inputs.has(p_id) ? active_tool_inputs[p_id] : Dictionary();
	const String status = _format_tool_progress(name, p_stage, input);
	update_tool_card(p_id, status, false);
}

void AIChatDock::_on_chat_message(const String &p_role, const String &p_text) {
	if (p_role == "You" || p_role == "user") {
		// User message already handled in _on_send_pressed
		return;
	}
	// Assistant response - stream into current turn
	append_response(p_text);
}

void AIChatDock::_on_context_updated(const Array &p_items) {
	_refresh_pinned_chips(p_items);
}

void AIChatDock::_update_meter() {
	String text;
	if (turn_tokens > 0 || session_tokens > 0) {
		text = "Turn: " + itos(turn_tokens);
		if (session_tokens > 0) {
			text += " | Session: " + itos(session_tokens);
		}
		// Always show cache rate so user knows if caching is working
		text += " | Cache: " + itos(cache_rate) + "%";
		if (tool_call_count > 0) {
			text += " | Tools: " + itos(tool_call_count);
		}
	} else {
		text = "Ready";
	}
	token_meter->set_text(text);
}

void AIChatDock::_on_scroll_value_changed(double p_value) {
	if (!scroll || !scroll->get_v_scroll_bar()) {
		stick_to_bottom = true;
		return;
	}

	const double max = scroll->get_v_scroll_bar()->get_max();
	if (max <= 0.0) {
		stick_to_bottom = true;
		return;
	}

	const double threshold = 8.0 * EDSCALE;
	stick_to_bottom = p_value >= (max - threshold);
}

void AIChatDock::_scroll_to_bottom() {
	if (!stick_to_bottom) {
		return;
	}
	callable_mp(this, &AIChatDock::_do_scroll_to_bottom).call_deferred();
}

void AIChatDock::_do_scroll_to_bottom() {
	if (!stick_to_bottom) {
		return;
	}
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
	// Accumulate raw markdown and re-render with conversion
	// This handles markdown syntax that spans multiple streaming chunks
	current_response_buffer += p_text;
	current_response_text->clear();
	current_response_text->append_text(_markdown_to_bbcode(current_response_buffer));
	_scroll_to_bottom();
}

void AIChatDock::add_tool_card(const String &p_id, const String &p_name, const Dictionary &p_input, const String &p_status) {
	_ensure_assistant_turn();

	// Add tool card directly to turn for proper chronological ordering
	PanelContainer *card = _create_tool_card(p_id, p_name, p_status);
	current_turn->add_child(card);
	active_tool_cards[p_id] = card;
	active_tool_names[p_id] = p_name;
	active_tool_inputs[p_id] = p_input;
	_scroll_to_bottom();
}

void AIChatDock::update_tool_card(const String &p_id, const String &p_status, bool p_done) {
	if (!active_tool_cards.has(p_id)) {
		return;
	}

	PanelContainer *card = active_tool_cards[p_id];
	Label *icon = active_tool_icons.has(p_id) ? active_tool_icons[p_id] : nullptr;
	Label *label = active_tool_labels.has(p_id) ? active_tool_labels[p_id] : nullptr;
	String name = active_tool_names.has(p_id) ? active_tool_names[p_id] : String();

	if (label) {
		const String display_name = name.is_empty() ? String("tool") : name;
		label->set_text(display_name + ": " + p_status);
	}

	if (p_done) {
		if (icon) {
			icon->set_text(U"✓");
		}
		tool_done_times[p_id] = OS::get_singleton()->get_ticks_msec();
		active_tool_icons.erase(p_id);

		// Change border to green for success
		Ref<StyleBoxFlat> done_style = theme_cache.tool_card_bg->duplicate();
		done_style->set_border_color(Color(0.4, 0.8, 0.4));
		card->add_theme_style_override("panel", done_style);
	}
}

void AIChatDock::update_usage(int64_t p_turn, int64_t p_session, int p_cache_rate) {
	turn_tokens = p_turn;
	session_tokens = p_session;
	cache_rate = p_cache_rate;
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
	current_thinking_toggle = nullptr;
	current_thinking_text = nullptr;
	thinking_collapsed = false;
	current_response_text = nullptr;
	current_response_buffer = "";
	current_tools_container = nullptr;
	has_thinking = false;
	has_response = false;

	// Reset per-turn tracking (keep cache_rate - it's session-level info)
	turn_tokens = 0;
	tool_call_count = 0;
	active_tool_cards.clear();
	active_tool_icons.clear();
	active_tool_labels.clear();
	active_tool_details.clear();
	active_tool_detail_containers.clear();
	active_tool_detail_toggles.clear();
	active_tool_inputs.clear();
	active_tool_names.clear();
	tool_done_times.clear();
	last_spinner_msec = 0;
	spinner_frame = 0;
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
	cache_rate = 0;
	_update_meter();
	_clear_context_chips();
	_clear_pinned_chips();
	_clear_image_attachments();
	if (mention_popup) {
		mention_popup->cancel();
	}
}
