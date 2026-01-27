/**************************************************************************/
/*  ai_runtime_dock.cpp                                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GAMEABLE                                   */
/*                     AI-powered game development                        */
/**************************************************************************/

#include "ai_runtime_dock.h"

#include "editor/ai/editor_ai_agent.h"
#include "editor/editor_node.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/check_box.h"
#include "scene/gui/label.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/rich_text_label.h"
#include "scene/gui/text_edit.h"
#include "scene/resources/style_box_flat.h"

void AIRuntimeDock::_bind_methods() {
	// No exposed methods yet
}

void AIRuntimeDock::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			_build_styles();

			if (EditorAIAgent *agent = EditorAIAgent::get_singleton()) {
				agent->connect("runtime_state_changed", callable_mp(this, &AIRuntimeDock::_on_runtime_state_changed));
				agent->connect("runtime_log_added", callable_mp(this, &AIRuntimeDock::_on_runtime_log_added));
				agent->connect("runtime_summary", callable_mp(this, &AIRuntimeDock::_on_runtime_summary));
				agent->connect("runtime_chat_message", callable_mp(this, &AIRuntimeDock::_on_runtime_chat_message));
				agent->connect("runtime_thinking", callable_mp(this, &AIRuntimeDock::_on_runtime_thinking));
				agent->connect("runtime_tool_call", callable_mp(this, &AIRuntimeDock::_on_runtime_tool_call));
				agent->connect("runtime_tool_result", callable_mp(this, &AIRuntimeDock::_on_runtime_tool_result));
				auto_fix_toggle->set_pressed(agent->is_runtime_auto_fix_enabled());
			}
		} break;

		case NOTIFICATION_THEME_CHANGED: {
			_build_styles();
		} break;
	}
}

AIRuntimeDock::AIRuntimeDock() {
	set_name("Live");
	set_h_size_flags(SIZE_EXPAND_FILL);
	set_v_size_flags(SIZE_EXPAND_FILL);
	_build_ui();
}

AIRuntimeDock::~AIRuntimeDock() {
}

void AIRuntimeDock::_build_ui() {
	set_v_size_flags(SIZE_EXPAND_FILL);
	add_theme_constant_override("separation", 8 * EDSCALE);

	_build_status();
	_build_controls();
	_build_attach_row();
	_build_note_section();
	_build_errors_section();
	_build_warnings_section();
	_build_logs_view();
	_build_agent_view();
}

void AIRuntimeDock::_build_styles() {
	Ref<Theme> theme = EditorNode::get_singleton() ? EditorNode::get_singleton()->get_editor_theme() : nullptr;
	if (!theme.is_valid()) {
		return;
	}

	Color base = theme->get_color("base_color", "Editor");
	Color panel = theme->get_color("dark_color_1", "Editor");
	float radius = 8 * EDSCALE;

	theme_cache.status_bg_idle.instantiate();
	theme_cache.status_bg_idle->set_bg_color(base.lightened(0.05));
	theme_cache.status_bg_idle->set_corner_radius_all(radius);

	theme_cache.status_bg_running.instantiate();
	theme_cache.status_bg_running->set_bg_color(Color(0.2, 0.55, 0.9));
	theme_cache.status_bg_running->set_corner_radius_all(radius);

	theme_cache.status_bg_stopped.instantiate();
	theme_cache.status_bg_stopped->set_bg_color(panel.darkened(0.1));
	theme_cache.status_bg_stopped->set_corner_radius_all(radius);

	theme_cache.card_bg_error.instantiate();
	theme_cache.card_bg_error->set_bg_color(Color(0.25, 0.08, 0.08));
	theme_cache.card_bg_error->set_corner_radius_all(radius);

	theme_cache.card_bg_warning.instantiate();
	theme_cache.card_bg_warning->set_bg_color(Color(0.28, 0.2, 0.08));
	theme_cache.card_bg_warning->set_corner_radius_all(radius);

	theme_cache.logs_bg.instantiate();
	theme_cache.logs_bg->set_bg_color(panel);
	theme_cache.logs_bg->set_corner_radius_all(radius);

	theme_cache.agent_bg.instantiate();
	theme_cache.agent_bg->set_bg_color(panel.darkened(0.05));
	theme_cache.agent_bg->set_corner_radius_all(radius);

	theme_cache.text_muted = theme->get_color("font_disabled_color", "Editor");
	theme_cache.corner_radius = radius;

	if (status_container) {
		status_container->add_theme_style_override(StringName("panel"), theme_cache.status_bg_idle);
	}
	if (logs_container) {
		logs_container->add_theme_style_override(StringName("panel"), theme_cache.logs_bg);
	}
	if (agent_container) {
		agent_container->add_theme_style_override(StringName("panel"), theme_cache.agent_bg);
	}
	if (scene_label) {
		scene_label->add_theme_color_override("font_color", theme_cache.text_muted);
	}
}

void AIRuntimeDock::_build_status() {
	status_container = memnew(PanelContainer);
	add_child(status_container);

	status_content = memnew(HBoxContainer);
	status_content->add_theme_constant_override("separation", 6 * EDSCALE);
	status_container->add_child(status_content);

	status_icon = memnew(Label);
	status_icon->set_text(U"●");
	status_icon->add_theme_font_size_override("font_size", 10 * EDSCALE);
	status_content->add_child(status_icon);

	status_label = memnew(Label);
	status_label->set_text("Idle");
	status_label->add_theme_font_size_override("font_size", 12 * EDSCALE);
	status_content->add_child(status_label);

	scene_label = memnew(Label);
	scene_label->set_text("Scene: -");
	scene_label->add_theme_font_size_override("font_size", 11 * EDSCALE);
	scene_label->add_theme_color_override("font_color", theme_cache.text_muted);
	add_child(scene_label);

	_set_status("Idle", "●", theme_cache.status_bg_idle);
}

void AIRuntimeDock::_build_controls() {
	controls_row = memnew(HBoxContainer);
	controls_row->add_theme_constant_override("separation", 8 * EDSCALE);
	add_child(controls_row);

	auto_fix_toggle = memnew(CheckBox);
	auto_fix_toggle->set_text("Auto-fix on stop");
	auto_fix_toggle->set_pressed(true);
	auto_fix_toggle->connect("toggled", callable_mp(this, &AIRuntimeDock::_on_autofix_toggled));
	controls_row->add_child(auto_fix_toggle);

	send_btn = memnew(Button);
	send_btn->set_text("Send errors to agent");
	send_btn->connect("pressed", callable_mp(this, &AIRuntimeDock::_on_send_pressed));
	controls_row->add_child(send_btn);
}

void AIRuntimeDock::_build_attach_row() {
	attach_row = memnew(HBoxContainer);
	attach_row->add_theme_constant_override("separation", 6 * EDSCALE);
	add_child(attach_row);

	attach_run_btn = memnew(Button);
	attach_run_btn->set_text("Attach last run");
	attach_run_btn->set_flat(true);
	attach_run_btn->connect("pressed", callable_mp(this, &AIRuntimeDock::_on_attach_run));
	attach_row->add_child(attach_run_btn);

	attach_errors_btn = memnew(Button);
	attach_errors_btn->set_text("Attach errors");
	attach_errors_btn->set_flat(true);
	attach_errors_btn->connect("pressed", callable_mp(this, &AIRuntimeDock::_on_attach_errors));
	attach_row->add_child(attach_errors_btn);

	attach_warnings_btn = memnew(Button);
	attach_warnings_btn->set_text("Attach warnings");
	attach_warnings_btn->set_flat(true);
	attach_warnings_btn->connect("pressed", callable_mp(this, &AIRuntimeDock::_on_attach_warnings));
	attach_row->add_child(attach_warnings_btn);

	attach_logs_btn = memnew(Button);
	attach_logs_btn->set_text("Attach logs");
	attach_logs_btn->set_flat(true);
	attach_logs_btn->connect("pressed", callable_mp(this, &AIRuntimeDock::_on_attach_logs));
	attach_row->add_child(attach_logs_btn);
}

void AIRuntimeDock::_build_note_section() {
	note_section = memnew(VBoxContainer);
	note_section->add_theme_constant_override("separation", 4 * EDSCALE);
	add_child(note_section);

	Label *note_label = memnew(Label);
	note_label->set_text("Runtime note");
	note_label->add_theme_font_size_override("font_size", 11 * EDSCALE);
	note_section->add_child(note_label);

	HBoxContainer *note_row = memnew(HBoxContainer);
	note_row->add_theme_constant_override("separation", 6 * EDSCALE);
	note_section->add_child(note_row);

	note_input = memnew(TextEdit);
	note_input->set_h_size_flags(SIZE_EXPAND_FILL);
	note_input->set_custom_minimum_size(Size2(0, 48 * EDSCALE));
	note_input->set_line_wrapping_mode(TextEdit::LINE_WRAPPING_BOUNDARY);
	note_input->set_placeholder("Describe what happened during Play...");
	note_row->add_child(note_input);

	attach_note_btn = memnew(Button);
	attach_note_btn->set_text("Attach note");
	attach_note_btn->set_flat(true);
	attach_note_btn->connect("pressed", callable_mp(this, &AIRuntimeDock::_on_attach_note));
	note_row->add_child(attach_note_btn);
}

void AIRuntimeDock::_build_errors_section() {
	errors_section = memnew(VBoxContainer);
	errors_section->add_theme_constant_override("separation", 4 * EDSCALE);
	add_child(errors_section);

	errors_toggle = memnew(Button);
	errors_toggle->set_flat(true);
	errors_toggle->set_text("Errors (0)");
	errors_toggle->connect("pressed", callable_mp(this, &AIRuntimeDock::_on_errors_toggle));
	errors_section->add_child(errors_toggle);

	errors_container = memnew(VBoxContainer);
	errors_container->add_theme_constant_override("separation", 4 * EDSCALE);
	errors_section->add_child(errors_container);
}

void AIRuntimeDock::_build_warnings_section() {
	warnings_section = memnew(VBoxContainer);
	warnings_section->add_theme_constant_override("separation", 4 * EDSCALE);
	add_child(warnings_section);

	warnings_toggle = memnew(Button);
	warnings_toggle->set_flat(true);
	warnings_toggle->set_text("Warnings (0)");
	warnings_toggle->connect("pressed", callable_mp(this, &AIRuntimeDock::_on_warnings_toggle));
	warnings_section->add_child(warnings_toggle);

	warnings_container = memnew(VBoxContainer);
	warnings_container->add_theme_constant_override("separation", 4 * EDSCALE);
	warnings_section->add_child(warnings_container);
}

void AIRuntimeDock::_build_logs_view() {
	logs_container = memnew(PanelContainer);
	logs_container->set_custom_minimum_size(Size2(0, 80 * EDSCALE));
	logs_container->set_v_size_flags(SIZE_FILL);
	logs_container->set_stretch_ratio(0.0);
	add_child(logs_container);

	logs_view = memnew(RichTextLabel);
	logs_view->set_use_bbcode(true);
	logs_view->set_scroll_active(true);
	logs_view->set_selection_enabled(true);
	logs_view->set_fit_content(false);
	logs_view->set_v_size_flags(SIZE_EXPAND_FILL);
	logs_container->add_child(logs_view);
}

void AIRuntimeDock::_build_agent_view() {
	Label *agent_label = memnew(Label);
	agent_label->set_text("Agent");
	agent_label->add_theme_font_size_override("font_size", 11 * EDSCALE);
	add_child(agent_label);

	agent_container = memnew(PanelContainer);
	agent_container->set_custom_minimum_size(Size2(0, 240 * EDSCALE));
	agent_container->set_v_size_flags(SIZE_EXPAND_FILL);
	agent_container->set_stretch_ratio(3.0);
	add_child(agent_container);

	agent_view = memnew(RichTextLabel);
	agent_view->set_use_bbcode(true);
	agent_view->set_scroll_active(true);
	agent_view->set_selection_enabled(true);
	agent_view->set_fit_content(false);
	agent_view->set_v_size_flags(SIZE_EXPAND_FILL);
	agent_container->add_child(agent_view);
}

void AIRuntimeDock::_on_errors_toggle() {
	errors_collapsed = !errors_collapsed;
	errors_container->set_visible(!errors_collapsed);
}

void AIRuntimeDock::_on_warnings_toggle() {
	warnings_collapsed = !warnings_collapsed;
	warnings_container->set_visible(!warnings_collapsed);
}

void AIRuntimeDock::_on_send_pressed() {
	if (EditorAIAgent *agent = EditorAIAgent::get_singleton()) {
		agent->request_runtime_fix(true);
	}
}

void AIRuntimeDock::_on_autofix_toggled(bool p_pressed) {
	if (EditorAIAgent *agent = EditorAIAgent::get_singleton()) {
		agent->set_runtime_auto_fix_enabled(p_pressed);
	}
}

void AIRuntimeDock::_on_attach_errors() {
	if (last_errors.is_empty()) {
		return;
	}
	String text = _join_tail(last_errors, 50);
	_attach_log_context("Runtime errors", text);
}

void AIRuntimeDock::_on_attach_warnings() {
	if (last_warnings.is_empty()) {
		return;
	}
	String text = _join_tail(last_warnings, 50);
	_attach_log_context("Runtime warnings", text);
}

void AIRuntimeDock::_on_attach_logs() {
	if (last_logs.is_empty()) {
		return;
	}
	String text = _join_tail(last_logs, 120);
	_attach_log_context("Runtime logs", text);
}

void AIRuntimeDock::_on_attach_run() {
	PackedStringArray lines;
	if (!last_errors.is_empty()) {
		lines.push_back("Errors:");
		for (const String &err : last_errors) {
			lines.push_back("- " + err);
		}
	}
	if (!last_warnings.is_empty()) {
		lines.push_back("Warnings:");
		for (const String &warn : last_warnings) {
			lines.push_back("- " + warn);
		}
	}
	if (!last_logs.is_empty()) {
		lines.push_back("Logs:");
		Vector<String> tail = last_logs;
		int start = MAX(0, tail.size() - 120);
		for (int i = start; i < tail.size(); i++) {
			lines.push_back(tail[i]);
		}
	}
	if (lines.is_empty()) {
		return;
	}
	_attach_log_context("Runtime run summary", String("\n").join(lines));
}

void AIRuntimeDock::_on_attach_note() {
	if (!note_input) {
		return;
	}
	String note = note_input->get_text().strip_edges();
	if (note.is_empty()) {
		return;
	}
	_attach_log_context("Runtime note", note);
	note_input->clear();
}

void AIRuntimeDock::_on_runtime_state_changed(const String &p_state, const String &p_scene) {
	if (p_state == "running") {
		_set_status("Running", "●", theme_cache.status_bg_running);
		_clear_results();
		logs_view->clear();
		agent_view->clear();
		agent_transcript.clear();
		runtime_stream_buffer.clear();
		runtime_stream_open = false;
		runtime_stream_role = "";
		last_errors.clear();
		last_warnings.clear();
		last_logs.clear();
	} else if (p_state == "stopped") {
		_set_status("Stopped", "●", theme_cache.status_bg_stopped);
		_finalize_runtime_stream_line();
	} else {
		_set_status("Idle", "●", theme_cache.status_bg_idle);
		_finalize_runtime_stream_line();
	}

	String scene_text = p_scene.is_empty() ? "Scene: -" : "Scene: " + p_scene;
	scene_label->set_text(scene_text);
	runtime_scene_path = p_scene;
}

void AIRuntimeDock::_on_runtime_log_added(const String &p_text, const String &p_level) {
	if (!logs_view) {
		return;
	}
	if (p_level == "error") {
		logs_view->push_color(Color(1.0, 0.5, 0.5));
	} else if (p_level == "warning") {
		logs_view->push_color(Color(1.0, 0.75, 0.4));
	}
	logs_view->add_text(p_text + "\n");
	if (p_level == "error" || p_level == "warning") {
		logs_view->pop();
	}
	logs_view->scroll_to_line(logs_view->get_line_count());
	last_logs.push_back(p_text);
}

void AIRuntimeDock::_on_runtime_summary(const Dictionary &p_summary) {
	_clear_results();
	int error_count = 0;
	int warning_count = 0;
	last_errors.clear();
	last_warnings.clear();
	last_logs.clear();

	if (p_summary.has("errors")) {
		Array errors = p_summary["errors"];
		error_count = errors.size();
		for (int i = 0; i < errors.size(); i++) {
			String text = String(errors[i]);
			last_errors.push_back(text);
			Label *lbl = memnew(Label);
			lbl->set_text(text);
			errors_container->add_child(lbl);
		}
	}
	if (p_summary.has("warnings")) {
		Array warnings = p_summary["warnings"];
		warning_count = warnings.size();
		for (int i = 0; i < warnings.size(); i++) {
			String text = String(warnings[i]);
			last_warnings.push_back(text);
			Label *lbl = memnew(Label);
			lbl->set_text(text);
			warnings_container->add_child(lbl);
		}
	}
	if (p_summary.has("logs")) {
		Array logs = p_summary["logs"];
		for (int i = 0; i < logs.size(); i++) {
			last_logs.push_back(String(logs[i]));
		}
	}

	errors_toggle->set_text("Errors (" + itos(error_count) + ")");
	warnings_toggle->set_text("Warnings (" + itos(warning_count) + ")");
}

void AIRuntimeDock::_on_runtime_chat_message(const String &p_role, const String &p_text) {
	if (p_role == "You") {
		_finalize_runtime_stream_line();
		_append_agent_line("You: " + p_text);
		runtime_stream_role = p_role;
		runtime_stream_open = false;
		return;
	}

	if (!runtime_stream_open || runtime_stream_role != p_role) {
		_finalize_runtime_stream_line();
		runtime_stream_role = p_role;
		runtime_stream_open = true;
		runtime_stream_buffer = p_role + ": ";
	}

	_append_agent_stream(p_text);
}

void AIRuntimeDock::_on_runtime_thinking(const String &p_text) {
	_finalize_runtime_stream_line();
	_append_agent_line("Thinking: " + p_text);
}

void AIRuntimeDock::_on_runtime_tool_call(const String &p_id, const String &p_name, const Dictionary &p_input) {
	(void)p_id;
	(void)p_input;
	_finalize_runtime_stream_line();
	_append_agent_line("Tool start: " + p_name);
}

void AIRuntimeDock::_on_runtime_tool_result(const String &p_id, bool p_ok, const Dictionary &p_output) {
	(void)p_output;
	_finalize_runtime_stream_line();
	_append_agent_line(String("Tool ") + (p_ok ? "ok: " : "fail: ") + p_id);
}

void AIRuntimeDock::_set_status(const String &p_status, const String &p_icon, const Ref<StyleBoxFlat> &p_style) {
	if (status_container && p_style.is_valid()) {
		status_container->add_theme_style_override(StringName("panel"), p_style);
	}
	if (status_icon) {
		status_icon->set_text(p_icon);
	}
	if (status_label) {
		status_label->set_text(p_status);
	}
}

void AIRuntimeDock::_clear_results() {
	if (errors_container) {
		while (errors_container->get_child_count() > 0) {
			Node *child = errors_container->get_child(0);
			errors_container->remove_child(child);
			memdelete(child);
		}
	}
	if (warnings_container) {
		while (warnings_container->get_child_count() > 0) {
			Node *child = warnings_container->get_child(0);
			warnings_container->remove_child(child);
			memdelete(child);
		}
	}
	errors_toggle->set_text("Errors (0)");
	warnings_toggle->set_text("Warnings (0)");
}

void AIRuntimeDock::_refresh_agent_view() {
	if (!agent_view) {
		return;
	}
	agent_view->set_text(agent_transcript + runtime_stream_buffer);
	agent_view->scroll_to_line(agent_view->get_line_count());
}

void AIRuntimeDock::_append_agent_text(const String &p_text, bool p_newline) {
	agent_transcript += p_text;
	if (p_newline) {
		agent_transcript += "\n";
	}
	_refresh_agent_view();
}

void AIRuntimeDock::_append_agent_line(const String &p_text) {
	_append_agent_text(p_text, true);
}

void AIRuntimeDock::_append_agent_stream(const String &p_text) {
	runtime_stream_buffer += p_text;
	_refresh_agent_view();
}

void AIRuntimeDock::_finalize_runtime_stream_line() {
	if (runtime_stream_open) {
		agent_transcript += runtime_stream_buffer;
		if (!agent_transcript.ends_with("\n")) {
			agent_transcript += "\n";
		}
		runtime_stream_buffer.clear();
		runtime_stream_open = false;
		_refresh_agent_view();
	}
}

String AIRuntimeDock::_join_tail(const Vector<String> &p_lines, int p_max_lines) const {
	if (p_lines.is_empty()) {
		return String();
	}
	int start = MAX(0, p_lines.size() - p_max_lines);
	PackedStringArray out;
	for (int i = start; i < p_lines.size(); i++) {
		out.push_back(p_lines[i]);
	}
	return String("\n").join(out);
}

void AIRuntimeDock::_attach_log_context(const String &p_label, const String &p_text) {
	if (p_text.is_empty()) {
		return;
	}
	if (EditorAIAgent *agent = EditorAIAgent::get_singleton()) {
		agent->add_log_context(p_label, p_text, "runtime", runtime_scene_path);
	}
}
