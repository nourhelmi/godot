/**************************************************************************/
/*  ai_chat_dock.cpp                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GAMEABLE                                   */
/*                     AI-powered game development                        */
/**************************************************************************/

#include "ai_chat_dock.h"

#include "ai_mention_popup.h"
#include "core/core_bind.h"
#include "core/input/input_event.h"
#include "core/io/resource_loader.h"
#include "core/os/os.h"
#include "core/string/translation.h"
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
#include "servers/display_server.h"

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

	// Send button
	send_btn = memnew(Button);
	send_btn->set_text(U"→");
	send_btn->set_tooltip_text("Send message (Enter)");
	send_btn->set_custom_minimum_size(Size2(36 * EDSCALE, 36 * EDSCALE));
	input_row->add_child(send_btn);

	send_btn->connect("pressed", callable_mp(this, &AIChatDock::_on_send_pressed));
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

	// Add inline for proper interleaving (no move_child - keeps chronological order)
	current_turn->add_child(current_thinking_block);

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
	}
}

void AIChatDock::_on_usage_updated(int64_t p_turn, int64_t p_session) {
	update_usage(p_turn, p_session);
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
	// Accumulate raw markdown and re-render with conversion
	// This handles markdown syntax that spans multiple streaming chunks
	current_response_buffer += p_text;
	current_response_text->clear();
	current_response_text->append_text(_markdown_to_bbcode(current_response_buffer));
	_scroll_to_bottom();
}

void AIChatDock::add_tool_card(const String &p_id, const String &p_name, const String &p_status) {
	_ensure_assistant_turn();

	// Add tool card directly to turn for proper chronological ordering
	PanelContainer *card = _create_tool_card(p_name, p_status);
	current_turn->add_child(card);
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
	current_thinking_toggle = nullptr;
	current_thinking_text = nullptr;
	thinking_collapsed = false;
	current_response_text = nullptr;
	current_response_buffer = "";
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
	_clear_context_chips();
	_clear_pinned_chips();
	_clear_image_attachments();
	if (mention_popup) {
		mention_popup->cancel();
	}
}
