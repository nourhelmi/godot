/**************************************************************************/
/*  ai_context_dock.h                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GAMEABLE                                   */
/*                     AI-powered game development                        */
/**************************************************************************/

#pragma once

#include "editor/ai/editor_ai_types.h"
#include "scene/gui/box_container.h"

class Button;
class FlowContainer;
class HBoxContainer;
class Label;
class LineEdit;
class OptionButton;
class PanelContainer;
class StyleBoxFlat;

// Beautiful context panel with chip-style items, elegant bundle management,
// and smooth interactions.
class AIContextDock : public VBoxContainer {
	GDCLASS(AIContextDock, VBoxContainer);

	// Theme cache
	struct ThemeCache {
		Ref<StyleBoxFlat> chip_bg;
		Ref<StyleBoxFlat> chip_bg_hover;
		Ref<StyleBoxFlat> section_bg;
		Ref<StyleBoxFlat> input_bg;
		Color accent_color;
		Color text_muted;
		int corner_radius = 6;
	} theme_cache;

	// Bundle section
	VBoxContainer *bundle_section = nullptr;
	OptionButton *bundle_dropdown = nullptr;
	HBoxContainer *save_row = nullptr;
	LineEdit *bundle_name_input = nullptr;
	Button *save_bundle_btn = nullptr;

	// Actions bar
	HBoxContainer *actions_bar = nullptr;
	Button *add_selection_btn = nullptr;
	Button *add_file_btn = nullptr;
	Button *clear_btn = nullptr;

	// Items area (flow layout with chips)
	PanelContainer *items_panel = nullptr;
	FlowContainer *items_flow = nullptr;

	// Footer
	Label *footer_label = nullptr;

	// Local state
	Vector<AIContextItem> pinned_items;
	Vector<String> bundle_names;

	// UI building
	void _build_ui();
	void _build_styles();
	void _build_bundle_section();
	void _build_actions_bar();
	void _build_items_area();
	void _build_footer();

	// Chip creation
	PanelContainer *_create_item_chip(const AIContextItem &p_item);

	// Event handlers
	void _on_bundle_selected(int p_idx);
	void _on_save_bundle();
	void _on_add_selection();
	void _on_add_file();
	void _on_clear_all();
	void _on_chip_remove(const String &p_item_id);
	void _on_chip_clicked(const String &p_item_id);

	// Agent signal handlers
	void _on_context_updated(const Array &p_items);
	void _on_bundles_updated(const PackedStringArray &p_names);

	void _refresh_items_ui();
	void _update_footer();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void refresh_bundles(const Vector<String> &p_names);
	void refresh_items(const Array &p_items);

	AIContextDock();
	~AIContextDock();
};
