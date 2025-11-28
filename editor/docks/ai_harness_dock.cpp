/**************************************************************************/
/*  ai_harness_dock.cpp                                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GAMEABLE                                   */
/*                     AI-powered game development                        */
/**************************************************************************/

#include "ai_harness_dock.h"

#include "core/config/project_settings.h"
#include "core/io/image.h"
#include "editor/ai/editor_ai_agent.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/rich_text_label.h"
#include "scene/gui/separator.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/texture_rect.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/style_box_flat.h"

void AIHarnessDock::_bind_methods() {
	// No exposed methods yet
}

void AIHarnessDock::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			_build_styles();

			// Connect to AI agent signals
			if (EditorAIAgent *agent = EditorAIAgent::get_singleton()) {
				agent->connect("verify_result", callable_mp(this, &AIHarnessDock::_on_verify_result));
				agent->connect("status", callable_mp(this, &AIHarnessDock::_on_status));
			}
		} break;

		case NOTIFICATION_THEME_CHANGED: {
			_build_styles();
		} break;
	}
}

AIHarnessDock::AIHarnessDock() {
	set_name("Harness");
	_build_ui();
}

AIHarnessDock::~AIHarnessDock() {
}

void AIHarnessDock::_build_ui() {
	add_theme_constant_override("separation", 10 * EDSCALE);

	_build_controls();
	_build_status();
	add_child(memnew(HSeparator));
	_build_errors_section();
	_build_warnings_section();
	add_child(memnew(HSeparator));
	_build_screenshot_preview();
	add_child(memnew(HSeparator));
	_build_logs_view();
}

void AIHarnessDock::_build_styles() {
	Ref<Theme> theme = EditorNode::get_singleton() ? EditorNode::get_singleton()->get_editor_theme() : nullptr;
	if (!theme.is_valid()) {
		return;
	}

	theme_cache.corner_radius = EDITOR_GET("interface/theme/corner_radius");
	float radius = theme_cache.corner_radius * EDSCALE;

	Color base_color = theme->get_color("base_color", "Editor");
	Color font_color = theme->get_color("font_color", "Editor");
	theme_cache.text_muted = Color(font_color.r, font_color.g, font_color.b, 0.6);

	// Status pill styles
	float pill_radius = 12 * EDSCALE;

	// Ready: subtle gray
	theme_cache.status_bg_ready.instantiate();
	theme_cache.status_bg_ready->set_bg_color(base_color.lightened(0.1));
	theme_cache.status_bg_ready->set_corner_radius_all(pill_radius);
	theme_cache.status_bg_ready->set_content_margin(SIDE_LEFT, 12 * EDSCALE);
	theme_cache.status_bg_ready->set_content_margin(SIDE_RIGHT, 12 * EDSCALE);
	theme_cache.status_bg_ready->set_content_margin(SIDE_TOP, 6 * EDSCALE);
	theme_cache.status_bg_ready->set_content_margin(SIDE_BOTTOM, 6 * EDSCALE);

	// Running: blue tint
	theme_cache.status_bg_running.instantiate();
	theme_cache.status_bg_running->set_bg_color(Color(0.2, 0.4, 0.8, 0.3));
	theme_cache.status_bg_running->set_corner_radius_all(pill_radius);
	theme_cache.status_bg_running->set_content_margin(SIDE_LEFT, 12 * EDSCALE);
	theme_cache.status_bg_running->set_content_margin(SIDE_RIGHT, 12 * EDSCALE);
	theme_cache.status_bg_running->set_content_margin(SIDE_TOP, 6 * EDSCALE);
	theme_cache.status_bg_running->set_content_margin(SIDE_BOTTOM, 6 * EDSCALE);

	// Passed: green tint
	theme_cache.status_bg_passed.instantiate();
	theme_cache.status_bg_passed->set_bg_color(Color(0.3, 0.7, 0.4, 0.3));
	theme_cache.status_bg_passed->set_corner_radius_all(pill_radius);
	theme_cache.status_bg_passed->set_content_margin(SIDE_LEFT, 12 * EDSCALE);
	theme_cache.status_bg_passed->set_content_margin(SIDE_RIGHT, 12 * EDSCALE);
	theme_cache.status_bg_passed->set_content_margin(SIDE_TOP, 6 * EDSCALE);
	theme_cache.status_bg_passed->set_content_margin(SIDE_BOTTOM, 6 * EDSCALE);

	// Failed: red tint
	theme_cache.status_bg_failed.instantiate();
	theme_cache.status_bg_failed->set_bg_color(Color(0.8, 0.3, 0.3, 0.3));
	theme_cache.status_bg_failed->set_corner_radius_all(pill_radius);
	theme_cache.status_bg_failed->set_content_margin(SIDE_LEFT, 12 * EDSCALE);
	theme_cache.status_bg_failed->set_content_margin(SIDE_RIGHT, 12 * EDSCALE);
	theme_cache.status_bg_failed->set_content_margin(SIDE_TOP, 6 * EDSCALE);
	theme_cache.status_bg_failed->set_content_margin(SIDE_BOTTOM, 6 * EDSCALE);

	// Error card: red left border
	theme_cache.card_bg_error.instantiate();
	theme_cache.card_bg_error->set_bg_color(base_color.lightened(0.05));
	theme_cache.card_bg_error->set_corner_radius_all(radius);
	theme_cache.card_bg_error->set_border_width(SIDE_LEFT, 3 * EDSCALE);
	theme_cache.card_bg_error->set_border_color(Color(0.9, 0.3, 0.3));
	theme_cache.card_bg_error->set_content_margin_all(8 * EDSCALE);
	theme_cache.card_bg_error->set_content_margin(SIDE_LEFT, 12 * EDSCALE);

	// Warning card: orange left border
	theme_cache.card_bg_warning.instantiate();
	theme_cache.card_bg_warning->set_bg_color(base_color.lightened(0.05));
	theme_cache.card_bg_warning->set_corner_radius_all(radius);
	theme_cache.card_bg_warning->set_border_width(SIDE_LEFT, 3 * EDSCALE);
	theme_cache.card_bg_warning->set_border_color(Color(0.9, 0.7, 0.2));
	theme_cache.card_bg_warning->set_content_margin_all(8 * EDSCALE);
	theme_cache.card_bg_warning->set_content_margin(SIDE_LEFT, 12 * EDSCALE);

	// Screenshot container
	theme_cache.screenshot_bg.instantiate();
	theme_cache.screenshot_bg->set_bg_color(base_color.darkened(0.1));
	theme_cache.screenshot_bg->set_corner_radius_all(radius);
	theme_cache.screenshot_bg->set_content_margin_all(4 * EDSCALE);

	// Logs container
	theme_cache.logs_bg.instantiate();
	theme_cache.logs_bg->set_bg_color(base_color.darkened(0.08));
	theme_cache.logs_bg->set_corner_radius_all(radius);
	theme_cache.logs_bg->set_content_margin_all(8 * EDSCALE);

	// Apply styles
	if (status_container) {
		status_container->add_theme_style_override("panel", theme_cache.status_bg_ready);
	}
	if (screenshot_container) {
		screenshot_container->add_theme_style_override("panel", theme_cache.screenshot_bg);
	}
	if (logs_container) {
		logs_container->add_theme_style_override("panel", theme_cache.logs_bg);
	}
}

void AIHarnessDock::_build_controls() {
	controls_row = memnew(HBoxContainer);
	controls_row->add_theme_constant_override("separation", 8 * EDSCALE);
	add_child(controls_row);

	frames_label = memnew(Label);
	frames_label->set_text("Frames:");
	controls_row->add_child(frames_label);

	frames_input = memnew(SpinBox);
	frames_input->set_min(1);
	frames_input->set_max(600);
	frames_input->set_value(60);
	frames_input->set_tooltip_text("Number of frames to run");
	controls_row->add_child(frames_input);

	controls_row->add_spacer();

	run_btn = memnew(Button);
	run_btn->set_text(U"▶ Run");
	run_btn->set_tooltip_text("Run headless verification");
	controls_row->add_child(run_btn);
	run_btn->connect("pressed", callable_mp(this, &AIHarnessDock::_on_run_pressed));
}

void AIHarnessDock::_build_status() {
	HBoxContainer *status_row = memnew(HBoxContainer);
	add_child(status_row);

	status_container = memnew(PanelContainer);
	status_row->add_child(status_container);

	status_content = memnew(HBoxContainer);
	status_content->add_theme_constant_override("separation", 6 * EDSCALE);
	status_container->add_child(status_content);

	status_icon = memnew(Label);
	status_icon->set_text(U"○");
	status_content->add_child(status_icon);

	status_label = memnew(Label);
	status_label->set_text("Ready");
	status_content->add_child(status_label);
}

void AIHarnessDock::_build_errors_section() {
	errors_section = memnew(VBoxContainer);
	errors_section->add_theme_constant_override("separation", 4 * EDSCALE);
	add_child(errors_section);

	errors_toggle = memnew(Button);
	errors_toggle->set_text(U"▼ Errors (0)");
	errors_toggle->set_text_alignment(HORIZONTAL_ALIGNMENT_LEFT);
	errors_toggle->set_flat(true);
	errors_toggle->add_theme_color_override("font_color", Color(0.9, 0.4, 0.4));
	errors_section->add_child(errors_toggle);
	errors_toggle->connect("pressed", callable_mp(this, &AIHarnessDock::_on_errors_toggle));

	errors_container = memnew(VBoxContainer);
	errors_container->add_theme_constant_override("separation", 4 * EDSCALE);
	errors_section->add_child(errors_container);
}

void AIHarnessDock::_build_warnings_section() {
	warnings_section = memnew(VBoxContainer);
	warnings_section->add_theme_constant_override("separation", 4 * EDSCALE);
	add_child(warnings_section);

	warnings_toggle = memnew(Button);
	warnings_toggle->set_text(U"▼ Warnings (0)");
	warnings_toggle->set_text_alignment(HORIZONTAL_ALIGNMENT_LEFT);
	warnings_toggle->set_flat(true);
	warnings_toggle->add_theme_color_override("font_color", Color(0.9, 0.7, 0.3));
	warnings_section->add_child(warnings_toggle);
	warnings_toggle->connect("pressed", callable_mp(this, &AIHarnessDock::_on_warnings_toggle));

	warnings_container = memnew(VBoxContainer);
	warnings_container->add_theme_constant_override("separation", 4 * EDSCALE);
	warnings_section->add_child(warnings_container);
}

void AIHarnessDock::_build_screenshot_preview() {
	Label *preview_label = memnew(Label);
	preview_label->set_text("Preview");
	preview_label->add_theme_font_size_override("font_size", 11 * EDSCALE);
	preview_label->add_theme_color_override("font_color", theme_cache.text_muted);
	add_child(preview_label);

	screenshot_container = memnew(PanelContainer);
	screenshot_container->set_custom_minimum_size(Size2(0, 120 * EDSCALE));
	add_child(screenshot_container);

	screenshot_preview = memnew(TextureRect);
	screenshot_preview->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
	screenshot_preview->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
	screenshot_container->add_child(screenshot_preview);
}

void AIHarnessDock::_build_logs_view() {
	Label *logs_label = memnew(Label);
	logs_label->set_text("Logs");
	logs_label->add_theme_font_size_override("font_size", 11 * EDSCALE);
	logs_label->add_theme_color_override("font_color", theme_cache.text_muted);
	add_child(logs_label);

	logs_container = memnew(PanelContainer);
	logs_container->set_v_size_flags(SIZE_EXPAND_FILL);
	add_child(logs_container);

	logs_view = memnew(RichTextLabel);
	logs_view->set_autowrap_mode(TextServer::AUTOWRAP_WORD);
	logs_view->set_selection_enabled(true);
	logs_view->set_v_size_flags(SIZE_EXPAND_FILL);

	// Monospace font for logs
	if (EditorNode::get_singleton()) {
		Ref<Theme> theme = EditorNode::get_singleton()->get_editor_theme();
		if (theme.is_valid()) {
			logs_view->add_theme_font_override("normal_font", theme->get_font("source", "EditorFonts"));
			logs_view->add_theme_font_size_override("normal_font_size", 11 * EDSCALE);
		}
	}
	logs_container->add_child(logs_view);
}

void AIHarnessDock::_on_run_pressed() {
	EditorAIAgent *agent = EditorAIAgent::get_singleton();
	if (!agent) {
		return;
	}

	// Get current scene path
	String scene_path;
	if (EditorNode::get_singleton()) {
		if (Node *scene_root = EditorNode::get_singleton()->get_edited_scene()) {
			scene_path = scene_root->get_scene_file_path();
		}
	}
	if (scene_path.is_empty()) {
		scene_path = EditorInterface::get_singleton()->get_current_path();
	}
	if (scene_path.is_empty()) {
		_set_status("No scene", U"⚠", theme_cache.status_bg_failed);
		return;
	}

	set_running(true);
	_clear_results();
	agent->run_harness(scene_path, (int)frames_input->get_value());
}

void AIHarnessDock::_on_errors_toggle() {
	errors_collapsed = !errors_collapsed;
	errors_container->set_visible(!errors_collapsed);
	// Update toggle text
	int count = errors_container->get_child_count();
	errors_toggle->set_text(String(errors_collapsed ? U"▶ " : U"▼ ") + "Errors (" + itos(count) + ")");
}

void AIHarnessDock::_on_warnings_toggle() {
	warnings_collapsed = !warnings_collapsed;
	warnings_container->set_visible(!warnings_collapsed);
	int count = warnings_container->get_child_count();
	warnings_toggle->set_text(String(warnings_collapsed ? U"▶ " : U"▼ ") + "Warnings (" + itos(count) + ")");
}

void AIHarnessDock::_on_verify_result(const Dictionary &p_result) {
	show_verify_result(p_result);
}

void AIHarnessDock::_on_status(const String &p_level, const String &p_message) {
	if (p_message.begins_with("harness:")) {
		append_log(p_message);
	}
}

void AIHarnessDock::_set_status(const String &p_status, const String &p_icon, const Ref<StyleBoxFlat> &p_style) {
	status_label->set_text(p_status);
	status_icon->set_text(p_icon);
	status_container->add_theme_style_override("panel", p_style);
}

void AIHarnessDock::_clear_results() {
	// Clear errors
	for (int i = errors_container->get_child_count() - 1; i >= 0; i--) {
		errors_container->get_child(i)->queue_free();
	}
	errors_toggle->set_text(U"▼ Errors (0)");

	// Clear warnings
	for (int i = warnings_container->get_child_count() - 1; i >= 0; i--) {
		warnings_container->get_child(i)->queue_free();
	}
	warnings_toggle->set_text(U"▼ Warnings (0)");

	// Clear logs
	logs_view->clear();

	// Clear screenshot
	screenshot_preview->set_texture(Ref<Texture2D>());
}

void AIHarnessDock::show_verify_result(const Dictionary &p_result) {
	set_running(false);
	_clear_results();

	// Status
	bool ok = p_result.has("ok") ? bool(p_result["ok"]) : false;
	if (ok) {
		_set_status("Passed", U"✓", theme_cache.status_bg_passed);
	} else {
		_set_status("Failed", U"✗", theme_cache.status_bg_failed);
	}

	// Errors
	if (p_result.has("errors")) {
		Array errors = p_result["errors"];
		for (int i = 0; i < errors.size(); i++) {
			PanelContainer *card = memnew(PanelContainer);
			card->add_theme_style_override("panel", theme_cache.card_bg_error);

			Label *lbl = memnew(Label);
			lbl->set_text(String(errors[i]));
			lbl->set_autowrap_mode(TextServer::AUTOWRAP_WORD);
			lbl->add_theme_font_size_override("font_size", 12 * EDSCALE);
			card->add_child(lbl);

			errors_container->add_child(card);
		}
		errors_toggle->set_text(String(U"▼ Errors (") + itos(errors.size()) + ")");
	}

	// Warnings
	if (p_result.has("warnings")) {
		Array warnings = p_result["warnings"];
		for (int i = 0; i < warnings.size(); i++) {
			PanelContainer *card = memnew(PanelContainer);
			card->add_theme_style_override("panel", theme_cache.card_bg_warning);

			Label *lbl = memnew(Label);
			lbl->set_text(String(warnings[i]));
			lbl->set_autowrap_mode(TextServer::AUTOWRAP_WORD);
			lbl->add_theme_font_size_override("font_size", 12 * EDSCALE);
			card->add_child(lbl);

			warnings_container->add_child(card);
		}
		warnings_toggle->set_text(String(U"▼ Warnings (") + itos(warnings.size()) + ")");
	}

	// Logs
	if (p_result.has("logs")) {
		Array logs = p_result["logs"];
		for (int i = 0; i < logs.size(); i++) {
			logs_view->append_text(String(logs[i]) + "\n");
		}
	}

	// Screenshot
	if (p_result.has("screenshot")) {
		String path = p_result["screenshot"];
		// Convert res:// to absolute path
		if (path.begins_with("res://")) {
			path = ProjectSettings::get_singleton()->globalize_path(path);
		}
		Ref<Image> img;
		img.instantiate();
		if (img->load(path) == OK) {
			Ref<ImageTexture> tex = ImageTexture::create_from_image(img);
			screenshot_preview->set_texture(tex);
		}
	}
}

void AIHarnessDock::append_log(const String &p_msg) {
	logs_view->append_text(p_msg + "\n");
}

void AIHarnessDock::set_running(bool p_running) {
	is_running = p_running;
	run_btn->set_disabled(p_running);

	if (p_running) {
		_set_status("Running...", U"◐", theme_cache.status_bg_running);
	}
}
