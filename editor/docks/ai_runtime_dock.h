/**************************************************************************/
/*  ai_runtime_dock.h                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GAMEABLE                                   */
/*                     AI-powered game development                        */
/**************************************************************************/

#pragma once

#include "scene/gui/box_container.h"

class Button;
class CheckBox;
class HBoxContainer;
class Label;
class PanelContainer;
class RichTextLabel;
class TextEdit;
class StyleBoxFlat;

// Live runtime panel: play loop status, error capture, and auto-fix controls.
class AIRuntimeDock : public VBoxContainer {
	GDCLASS(AIRuntimeDock, VBoxContainer);

	// Theme cache
	struct ThemeCache {
		Ref<StyleBoxFlat> status_bg_idle;
		Ref<StyleBoxFlat> status_bg_running;
		Ref<StyleBoxFlat> status_bg_stopped;
		Ref<StyleBoxFlat> card_bg_error;
		Ref<StyleBoxFlat> card_bg_warning;
		Ref<StyleBoxFlat> logs_bg;
		Ref<StyleBoxFlat> agent_bg;
		Color text_muted;
		int corner_radius = 6;
	} theme_cache;

	// Status pill
	PanelContainer *status_container = nullptr;
	HBoxContainer *status_content = nullptr;
	Label *status_icon = nullptr;
	Label *status_label = nullptr;
	Label *scene_label = nullptr;

	// Controls row
	HBoxContainer *controls_row = nullptr;
	CheckBox *auto_fix_toggle = nullptr;
	Button *send_btn = nullptr;

	// Attach row
	HBoxContainer *attach_row = nullptr;
	Button *attach_errors_btn = nullptr;
	Button *attach_warnings_btn = nullptr;
	Button *attach_logs_btn = nullptr;
	Button *attach_run_btn = nullptr;

	// Runtime note
	VBoxContainer *note_section = nullptr;
	TextEdit *note_input = nullptr;
	Button *attach_note_btn = nullptr;

	// Errors section
	VBoxContainer *errors_section = nullptr;
	Button *errors_toggle = nullptr;
	VBoxContainer *errors_container = nullptr;
	bool errors_collapsed = false;

	// Warnings section
	VBoxContainer *warnings_section = nullptr;
	Button *warnings_toggle = nullptr;
	VBoxContainer *warnings_container = nullptr;
	bool warnings_collapsed = false;

	// Logs view
	PanelContainer *logs_container = nullptr;
	RichTextLabel *logs_view = nullptr;

	// Agent chat view
	PanelContainer *agent_container = nullptr;
	RichTextLabel *agent_view = nullptr;
	String agent_transcript;
	String runtime_stream_buffer;
	String runtime_stream_role;
	bool runtime_stream_open = false;
	String runtime_scene_path;
	Vector<String> last_errors;
	Vector<String> last_warnings;
	Vector<String> last_logs;

	// UI building
	void _build_ui();
	void _build_styles();
	void _build_status();
	void _build_controls();
	void _build_attach_row();
	void _build_note_section();
	void _build_errors_section();
	void _build_warnings_section();
	void _build_logs_view();
	void _build_agent_view();

	// Event handlers
	void _on_errors_toggle();
	void _on_warnings_toggle();
	void _on_send_pressed();
	void _on_autofix_toggled(bool p_pressed);
	void _on_attach_errors();
	void _on_attach_warnings();
	void _on_attach_logs();
	void _on_attach_run();
	void _on_attach_note();

	// Agent signal handlers
	void _on_runtime_state_changed(const String &p_state, const String &p_scene);
	void _on_runtime_log_added(const String &p_text, const String &p_level);
	void _on_runtime_summary(const Dictionary &p_summary);
	void _on_runtime_chat_message(const String &p_role, const String &p_text);
	void _on_runtime_thinking(const String &p_text);
	void _on_runtime_tool_call(const String &p_id, const String &p_name, const Dictionary &p_input);
	void _on_runtime_tool_result(const String &p_id, bool p_ok, const Dictionary &p_output, bool p_preliminary);

	void _set_status(const String &p_status, const String &p_icon, const Ref<StyleBoxFlat> &p_style);
	void _clear_results();
	void _refresh_agent_view();
	void _append_agent_text(const String &p_text, bool p_newline);
	void _append_agent_line(const String &p_text);
	void _append_agent_stream(const String &p_text);
	void _finalize_runtime_stream_line();
	String _join_tail(const Vector<String> &p_lines, int p_max_lines) const;
	void _attach_log_context(const String &p_label, const String &p_text);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	AIRuntimeDock();
	~AIRuntimeDock();
};
