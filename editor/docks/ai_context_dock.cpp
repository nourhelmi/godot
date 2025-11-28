/**************************************************************************/
/*  ai_context_dock.cpp                                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GAMEABLE                                   */
/*                     AI-powered game development                        */
/**************************************************************************/

#include "ai_context_dock.h"

#include "core/io/resource_loader.h"
#include "editor/ai/editor_ai_agent.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/flow_container.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/option_button.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/separator.h"
#include "scene/gui/texture_rect.h"
#include "scene/resources/style_box_flat.h"

void AIContextDock::_bind_methods() {
	// No exposed methods yet
}

void AIContextDock::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			_build_styles();

			// Connect to AI agent signals
			if (EditorAIAgent *agent = EditorAIAgent::get_singleton()) {
				agent->connect("context_updated", callable_mp(this, &AIContextDock::_on_context_updated));
				agent->connect("bundles_updated", callable_mp(this, &AIContextDock::_on_bundles_updated));
			}
		} break;

		case NOTIFICATION_THEME_CHANGED: {
			_build_styles();
			_refresh_items_ui();
		} break;
	}
}

AIContextDock::AIContextDock() {
	set_name("Context");
	_build_ui();
}

AIContextDock::~AIContextDock() {
}

void AIContextDock::_build_ui() {
	add_theme_constant_override("separation", 8 * EDSCALE);

	_build_bundle_section();
	add_child(memnew(HSeparator));
	_build_actions_bar();
	_build_items_area();
	_build_footer();
}

void AIContextDock::_build_styles() {
	Ref<Theme> theme = EditorNode::get_singleton() ? EditorNode::get_singleton()->get_editor_theme() : nullptr;
	if (!theme.is_valid()) {
		return;
	}

	theme_cache.corner_radius = EDITOR_GET("interface/theme/corner_radius");
	float radius = theme_cache.corner_radius * EDSCALE;

	Color base_color = theme->get_color("base_color", "Editor");
	Color accent_color = theme->get_color("accent_color", "Editor");
	Color font_color = theme->get_color("font_color", "Editor");
	theme_cache.accent_color = accent_color;
	theme_cache.text_muted = Color(font_color.r, font_color.g, font_color.b, 0.6);

	// Chip: pill-shaped, subtle background
	theme_cache.chip_bg.instantiate();
	theme_cache.chip_bg->set_bg_color(base_color.lightened(0.1));
	theme_cache.chip_bg->set_corner_radius_all(14 * EDSCALE); // Pill shape
	theme_cache.chip_bg->set_content_margin(SIDE_LEFT, 10 * EDSCALE);
	theme_cache.chip_bg->set_content_margin(SIDE_RIGHT, 6 * EDSCALE);
	theme_cache.chip_bg->set_content_margin(SIDE_TOP, 4 * EDSCALE);
	theme_cache.chip_bg->set_content_margin(SIDE_BOTTOM, 4 * EDSCALE);

	// Chip hover: slightly brighter
	theme_cache.chip_bg_hover.instantiate();
	theme_cache.chip_bg_hover->set_bg_color(base_color.lightened(0.18));
	theme_cache.chip_bg_hover->set_corner_radius_all(14 * EDSCALE);
	theme_cache.chip_bg_hover->set_content_margin(SIDE_LEFT, 10 * EDSCALE);
	theme_cache.chip_bg_hover->set_content_margin(SIDE_RIGHT, 6 * EDSCALE);
	theme_cache.chip_bg_hover->set_content_margin(SIDE_TOP, 4 * EDSCALE);
	theme_cache.chip_bg_hover->set_content_margin(SIDE_BOTTOM, 4 * EDSCALE);

	// Items container: subtle inset
	theme_cache.section_bg.instantiate();
	theme_cache.section_bg->set_bg_color(base_color.darkened(0.05));
	theme_cache.section_bg->set_corner_radius_all(radius);
	theme_cache.section_bg->set_content_margin_all(8 * EDSCALE);

	// Input background
	theme_cache.input_bg.instantiate();
	theme_cache.input_bg->set_bg_color(base_color.darkened(0.05));
	theme_cache.input_bg->set_corner_radius_all(radius);
	theme_cache.input_bg->set_content_margin_all(6 * EDSCALE);

	// Apply styles
	if (items_panel) {
		items_panel->add_theme_style_override("panel", theme_cache.section_bg);
	}
}

void AIContextDock::_build_bundle_section() {
	bundle_section = memnew(VBoxContainer);
	bundle_section->add_theme_constant_override("separation", 6 * EDSCALE);
	add_child(bundle_section);

	// Bundle dropdown row
	HBoxContainer *dropdown_row = memnew(HBoxContainer);
	bundle_section->add_child(dropdown_row);

	Label *bundle_label = memnew(Label);
	bundle_label->set_text("Bundle:");
	bundle_label->add_theme_font_size_override("font_size", 12 * EDSCALE);
	dropdown_row->add_child(bundle_label);

	bundle_dropdown = memnew(OptionButton);
	bundle_dropdown->set_h_size_flags(SIZE_EXPAND_FILL);
	bundle_dropdown->set_tooltip_text("Load a saved context bundle");
	bundle_dropdown->add_item("(none)");
	bundle_dropdown->set_flat(true);
	dropdown_row->add_child(bundle_dropdown);
	bundle_dropdown->connect("item_selected", callable_mp(this, &AIContextDock::_on_bundle_selected));

	// Save bundle row
	save_row = memnew(HBoxContainer);
	save_row->add_theme_constant_override("separation", 6 * EDSCALE);
	bundle_section->add_child(save_row);

	bundle_name_input = memnew(LineEdit);
	bundle_name_input->set_placeholder("Save as...");
	bundle_name_input->set_h_size_flags(SIZE_EXPAND_FILL);
	save_row->add_child(bundle_name_input);

	save_bundle_btn = memnew(Button);
	save_bundle_btn->set_text("Save");
	save_bundle_btn->set_tooltip_text("Save current context as bundle");
	save_bundle_btn->set_flat(true);
	save_row->add_child(save_bundle_btn);
	save_bundle_btn->connect("pressed", callable_mp(this, &AIContextDock::_on_save_bundle));
}

void AIContextDock::_build_actions_bar() {
	actions_bar = memnew(HBoxContainer);
	actions_bar->add_theme_constant_override("separation", 6 * EDSCALE);
	add_child(actions_bar);

	add_selection_btn = memnew(Button);
	add_selection_btn->set_text("+ Selection");
	add_selection_btn->set_tooltip_text("Pin current editor selection");
	add_selection_btn->set_flat(true);
	actions_bar->add_child(add_selection_btn);
	add_selection_btn->connect("pressed", callable_mp(this, &AIContextDock::_on_add_selection));

	add_file_btn = memnew(Button);
	add_file_btn->set_text("+ File");
	add_file_btn->set_tooltip_text("Pin current file");
	add_file_btn->set_flat(true);
	actions_bar->add_child(add_file_btn);
	add_file_btn->connect("pressed", callable_mp(this, &AIContextDock::_on_add_file));

	actions_bar->add_spacer();

	clear_btn = memnew(Button);
	clear_btn->set_text("Clear");
	clear_btn->set_tooltip_text("Clear all pinned items");
	clear_btn->set_flat(true);
	actions_bar->add_child(clear_btn);
	clear_btn->connect("pressed", callable_mp(this, &AIContextDock::_on_clear_all));
}

void AIContextDock::_build_items_area() {
	items_panel = memnew(PanelContainer);
	items_panel->set_v_size_flags(SIZE_EXPAND_FILL);
	add_child(items_panel);

	items_flow = memnew(FlowContainer);
	items_flow->set_h_size_flags(SIZE_EXPAND_FILL);
	items_flow->add_theme_constant_override("h_separation", 6 * EDSCALE);
	items_flow->add_theme_constant_override("v_separation", 6 * EDSCALE);
	items_panel->add_child(items_flow);
}

void AIContextDock::_build_footer() {
	footer_label = memnew(Label);
	footer_label->set_text("No items pinned");
	footer_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	footer_label->add_theme_font_size_override("font_size", 11 * EDSCALE);
	footer_label->add_theme_color_override("font_color", theme_cache.text_muted);
	add_child(footer_label);
}

PanelContainer *AIContextDock::_create_item_chip(const AIContextItem &p_item) {
	PanelContainer *chip = memnew(PanelContainer);
	chip->add_theme_style_override("panel", theme_cache.chip_bg);
	chip->set_tooltip_text(p_item.path);
	chip->set_meta("item_id", p_item.id);

	HBoxContainer *content = memnew(HBoxContainer);
	content->add_theme_constant_override("separation", 6 * EDSCALE);
	chip->add_child(content);

	// Icon
	Ref<Theme> theme = EditorNode::get_singleton() ? EditorNode::get_singleton()->get_editor_theme() : nullptr;
	if (theme.is_valid()) {
		TextureRect *icon = memnew(TextureRect);
		String icon_name = ai_context_kind_to_icon(p_item.kind);
		icon->set_texture(theme->get_icon(icon_name, "EditorIcons"));
		icon->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
		icon->set_custom_minimum_size(Size2(16, 16) * EDSCALE);
		content->add_child(icon);
	}

	// Label (clickable to navigate)
	Button *label_btn = memnew(Button);
	label_btn->set_text(p_item.label.is_empty() ? p_item.path.get_file() : p_item.label);
	label_btn->set_flat(true);
	label_btn->add_theme_font_size_override("font_size", 12 * EDSCALE);
	label_btn->set_tooltip_text("Click to navigate");

	label_btn->connect("pressed", callable_mp(this, &AIContextDock::_on_chip_clicked).bind(p_item.id));
	content->add_child(label_btn);

	// Remove button (×)
	Button *remove_btn = memnew(Button);
	remove_btn->set_text(U"×");
	remove_btn->set_flat(true);
	remove_btn->set_tooltip_text("Remove from context");
	remove_btn->add_theme_font_size_override("font_size", 14 * EDSCALE);
	remove_btn->connect("pressed", callable_mp(this, &AIContextDock::_on_chip_remove).bind(p_item.id));
	content->add_child(remove_btn);

	return chip;
}

void AIContextDock::_on_bundle_selected(int p_idx) {
	if (p_idx <= 0) {
		return;
	}
	String name = bundle_dropdown->get_item_text(p_idx);
	if (EditorAIAgent *agent = EditorAIAgent::get_singleton()) {
		agent->load_bundle(name);
	}
}

void AIContextDock::_on_save_bundle() {
	String name = bundle_name_input->get_text().strip_edges();
	if (name.is_empty()) {
		return;
	}
	if (EditorAIAgent *agent = EditorAIAgent::get_singleton()) {
		agent->save_bundle(name);
	}
	bundle_name_input->clear();
}

void AIContextDock::_on_add_selection() {
	if (!EditorNode::get_singleton()) {
		return;
	}
	EditorAIAgent *agent = EditorAIAgent::get_singleton();
	if (!agent) {
		return;
	}

	// Get current selection
	if (EditorSelection *es = EditorNode::get_singleton()->get_editor_selection()) {
		List<Node *> nodes = es->get_full_selected_node_list();
		for (List<Node *>::Element *E = nodes.front(); E; E = E->next()) {
			Node *n = E->get();
			if (!n) {
				continue;
			}
			agent->add_context_item(AI_CONTEXT_NODE, String(n->get_path()), n->get_name());
		}
	}
}

void AIContextDock::_on_add_file() {
	EditorAIAgent *agent = EditorAIAgent::get_singleton();
	if (!agent) {
		return;
	}

	String path = EditorInterface::get_singleton()->get_current_path();
	if (path.is_empty()) {
		return;
	}

	AIContextItemKind kind = ai_context_kind_from_path(path);
	agent->add_context_item(kind, path, path.get_file());
}

void AIContextDock::_on_clear_all() {
	if (EditorAIAgent *agent = EditorAIAgent::get_singleton()) {
		agent->clear_context();
	}
}

void AIContextDock::_on_chip_remove(const String &p_item_id) {
	if (EditorAIAgent *agent = EditorAIAgent::get_singleton()) {
		agent->remove_context_item(p_item_id);
	}
}

void AIContextDock::_on_chip_clicked(const String &p_item_id) {
	// Find item by ID
	const AIContextItem *item = nullptr;
	for (const AIContextItem &it : pinned_items) {
		if (it.id == p_item_id) {
			item = &it;
			break;
		}
	}
	if (!item) {
		return;
	}

	// Navigate to item in editor
	if (item->kind == AI_CONTEXT_SCENE) {
		EditorInterface::get_singleton()->open_scene_from_path(item->path);
	} else if (item->kind == AI_CONTEXT_NODE) {
		// Select node in scene tree
		if (Node *root = EditorNode::get_singleton()->get_edited_scene()) {
			if (Node *n = root->get_node_or_null(item->path)) {
				EditorNode::get_singleton()->get_editor_selection()->clear();
				EditorNode::get_singleton()->get_editor_selection()->add_node(n);
			}
		}
	} else {
		// Open as resource
		Ref<Resource> res = ResourceLoader::load(item->path);
		if (res.is_valid()) {
			EditorInterface::get_singleton()->edit_resource(res);
		} else {
			EditorInterface::get_singleton()->select_file(item->path);
		}
	}
}

void AIContextDock::_on_context_updated(const Array &p_items) {
	refresh_items(p_items);
}

void AIContextDock::_on_bundles_updated(const PackedStringArray &p_names) {
	Vector<String> names;
	for (int i = 0; i < p_names.size(); i++) {
		names.push_back(p_names[i]);
	}
	refresh_bundles(names);
}

void AIContextDock::_refresh_items_ui() {
	// Clear existing chips
	for (int i = items_flow->get_child_count() - 1; i >= 0; i--) {
		items_flow->get_child(i)->queue_free();
	}

	// Create new chips
	for (const AIContextItem &item : pinned_items) {
		PanelContainer *chip = _create_item_chip(item);
		items_flow->add_child(chip);
	}

	_update_footer();
}

void AIContextDock::_update_footer() {
	int count = pinned_items.size();
	if (count == 0) {
		footer_label->set_text("No items pinned");
	} else if (count == 1) {
		footer_label->set_text("1 item pinned");
	} else {
		footer_label->set_text(itos(count) + " items pinned");
	}
}

void AIContextDock::refresh_bundles(const Vector<String> &p_names) {
	bundle_names = p_names;
	bundle_dropdown->clear();
	bundle_dropdown->add_item("(none)");
	for (const String &name : p_names) {
		bundle_dropdown->add_item(name);
	}
}

void AIContextDock::refresh_items(const Array &p_items) {
	pinned_items.clear();
	for (int i = 0; i < p_items.size(); i++) {
		pinned_items.push_back(AIContextItem::from_dict(p_items[i]));
	}
	_refresh_items_ui();
}
