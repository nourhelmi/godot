/**************************************************************************/
/*  ai_harness_dock.h                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GAMEABLE                                   */
/*                     AI-powered game development                        */
/**************************************************************************/

#pragma once

#include "scene/gui/box_container.h"

class Button;
class HBoxContainer;
class Label;
class PanelContainer;
class RichTextLabel;
class SpinBox;
class StyleBoxFlat;
class TextureRect;

// Beautiful harness panel with dashboard-style status, expandable error/warning
// cards, screenshot preview, and clean log output.
class AIHarnessDock : public VBoxContainer {
	GDCLASS(AIHarnessDock, VBoxContainer);

	// Theme cache
	struct ThemeCache {
		Ref<StyleBoxFlat> status_bg_ready;
		Ref<StyleBoxFlat> status_bg_running;
		Ref<StyleBoxFlat> status_bg_passed;
		Ref<StyleBoxFlat> status_bg_failed;
		Ref<StyleBoxFlat> card_bg_error;
		Ref<StyleBoxFlat> card_bg_warning;
		Ref<StyleBoxFlat> screenshot_bg;
		Ref<StyleBoxFlat> logs_bg;
		Color text_muted;
		int corner_radius = 6;
	} theme_cache;

	// Controls row
	HBoxContainer *controls_row = nullptr;
	Label *frames_label = nullptr;
	SpinBox *frames_input = nullptr;
	Button *run_btn = nullptr;

	// Status pill
	PanelContainer *status_container = nullptr;
	HBoxContainer *status_content = nullptr;
	Label *status_icon = nullptr;
	Label *status_label = nullptr;

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

	// Screenshot preview
	PanelContainer *screenshot_container = nullptr;
	TextureRect *screenshot_preview = nullptr;

	// Logs view
	PanelContainer *logs_container = nullptr;
	RichTextLabel *logs_view = nullptr;

	// State
	bool is_running = false;

	// UI building
	void _build_ui();
	void _build_styles();
	void _build_controls();
	void _build_status();
	void _build_errors_section();
	void _build_warnings_section();
	void _build_screenshot_preview();
	void _build_logs_view();

	// Event handlers
	void _on_run_pressed();
	void _on_errors_toggle();
	void _on_warnings_toggle();

	// Agent signal handlers
	void _on_verify_result(const Dictionary &p_result);
	void _on_status(const String &p_level, const String &p_message);

	void _set_status(const String &p_status, const String &p_icon, const Ref<StyleBoxFlat> &p_style);
	void _clear_results();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void show_verify_result(const Dictionary &p_result);
	void append_log(const String &p_msg);
	void set_running(bool p_running);

	AIHarnessDock();
	~AIHarnessDock();
};
