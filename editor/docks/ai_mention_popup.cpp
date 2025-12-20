/**************************************************************************/
/*  ai_mention_popup.cpp                                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GAMEABLE                                   */
/*                     AI-powered game development                        */
/**************************************************************************/

#include "ai_mention_popup.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/string/char_utils.h"
#include "editor/editor_node.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/item_list.h"
#include "scene/resources/style_box_flat.h"

void AIMentionPopup::_bind_methods() {
	ADD_SIGNAL(MethodInfo("mention_selected",
			PropertyInfo(Variant::STRING, "path"),
			PropertyInfo(Variant::STRING, "label"),
			PropertyInfo(Variant::INT, "start_col")));
}

AIMentionPopup::AIMentionPopup() {
	// Use top-level positioning - ignore parent VBoxContainer layout
	set_as_top_level(true);

	// Style the panel to look like a popup
	Ref<StyleBoxFlat> panel_style;
	panel_style.instantiate();
	panel_style->set_bg_color(Color(0.15, 0.15, 0.18, 0.98));
	panel_style->set_corner_radius_all(6 * EDSCALE);
	panel_style->set_border_width_all(1 * EDSCALE);
	panel_style->set_border_color(Color(0.3, 0.3, 0.35));
	panel_style->set_content_margin_all(4 * EDSCALE);
	panel_style->set_shadow_color(Color(0, 0, 0, 0.3));
	panel_style->set_shadow_size(8 * EDSCALE);
	add_theme_style_override("panel", panel_style);

	set_custom_minimum_size(Size2(300 * EDSCALE, 0));
	set_mouse_filter(MOUSE_FILTER_STOP); // Capture mouse for clicks

	// Create item list
	item_list = memnew(ItemList);
	item_list->set_max_columns(1);
	item_list->set_select_mode(ItemList::SELECT_SINGLE);
	item_list->set_allow_reselect(true);
	item_list->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
	item_list->set_focus_mode(FOCUS_NONE); // Critical: don't take focus from input
	item_list->set_v_size_flags(SIZE_EXPAND_FILL);
	item_list->connect("item_activated", callable_mp(this, &AIMentionPopup::_emit_selected));
	add_child(item_list);

	hide();
}

bool AIMentionPopup::update_filter(const String &p_text_before_caret, int p_caret_col) {
	// Find last @ in text before caret
	int at_pos = p_text_before_caret.rfind("@");
	if (at_pos == -1) {
		cancel();
		return false;
	}

	// @ must be preceded by whitespace or be at start
	if (at_pos > 0 && !is_whitespace(p_text_before_caret[at_pos - 1])) {
		cancel();
		return false;
	}

	// Extract filter (text after @) - can be empty
	mention_start_col = at_pos;
	current_filter = p_text_before_caret.substr(at_pos + 1);

	// Populate and return whether we have items
	_populate(current_filter);
	return items.size() > 0;
}

void AIMentionPopup::show_at(Control *p_anchor) {
	if (!p_anchor || items.size() == 0) {
		hide();
		return;
	}

	// Update item list
	item_list->clear();
	for (const MentionItem &item : items) {
		item_list->add_item(item.display);
	}

	// Select first item
	if (item_list->get_item_count() > 0) {
		item_list->select(0);
		item_list->ensure_current_is_visible();
	}

	// Calculate size
	float item_height = 24 * EDSCALE;
	float max_height = 200 * EDSCALE;
	float height = MIN(items.size() * item_height + 8 * EDSCALE, max_height);
	float width = 350 * EDSCALE;
	set_custom_minimum_size(Size2(width, height));
	set_size(Size2(width, height));

	// Position ABOVE the anchor using global coordinates (we're top-level)
	Vector2 anchor_global = p_anchor->get_global_position();

	Vector2 pos;
	pos.x = anchor_global.x;
	// Position above: anchor's top minus popup height minus gap
	pos.y = anchor_global.y - height - 4 * EDSCALE;

	// Clamp to not go above screen top
	if (pos.y < 0) {
		pos.y = 0;
	}

	set_global_position(pos);
	show();
}

void AIMentionPopup::cancel() {
	hide();
	mention_start_col = -1;
	current_filter = "";
	items.clear();
}

bool AIMentionPopup::handle_key(Key p_keycode) {
	if (!is_visible() || items.size() == 0) {
		return false;
	}

	int current = item_list->get_item_count() > 0 ? MAX(0, item_list->get_current()) : 0;

	switch (p_keycode) {
		case Key::UP: {
			int new_idx = MAX(0, current - 1);
			item_list->select(new_idx);
			item_list->ensure_current_is_visible();
			return true;
		}
		case Key::DOWN: {
			int new_idx = MIN(item_list->get_item_count() - 1, current + 1);
			item_list->select(new_idx);
			item_list->ensure_current_is_visible();
			return true;
		}
		case Key::ENTER:
		case Key::KP_ENTER:
		case Key::TAB: {
			_emit_selected(current);
			return true;
		}
		case Key::ESCAPE: {
			cancel();
			return true;
		}
		default:
			return false;
	}
}

void AIMentionPopup::_emit_selected(int p_idx) {
	if (p_idx < 0 || p_idx >= items.size()) {
		return;
	}

	const MentionItem &item = items[p_idx];
	String label;

	// Extract clean label for display
	if (item.path.begins_with("node:")) {
		String node_path = item.path.substr(5);
		label = node_path.get_file();
		if (label.is_empty()) {
			label = node_path;
		}
	} else {
		label = item.path.get_file();
	}

	emit_signal("mention_selected", item.path, label, mention_start_col);
	cancel();
}

void AIMentionPopup::_populate(const String &p_filter) {
	items.clear();

	String filter_lower = p_filter.to_lower();
	const int MAX_ITEMS = 12;

	// 1. Search project files matching filter
	Vector<String> extensions = { "gd", "tscn", "scn", "tres", "res", "shader", "gdshader" };
	String project_path = ProjectSettings::get_singleton()->get_resource_path();

	Ref<DirAccess> da = DirAccess::open(project_path);
	if (da.is_valid()) {
		Vector<String> dirs_to_scan;
		dirs_to_scan.push_back(project_path);

		while (dirs_to_scan.size() > 0 && items.size() < MAX_ITEMS) {
			String current_dir = dirs_to_scan[dirs_to_scan.size() - 1];
			dirs_to_scan.remove_at(dirs_to_scan.size() - 1);

			Ref<DirAccess> dir = DirAccess::open(current_dir);
			if (!dir.is_valid()) {
				continue;
			}

			dir->list_dir_begin();
			String file = dir->get_next();
			while (!file.is_empty() && items.size() < MAX_ITEMS) {
				if (file == "." || file == ".." || file.begins_with(".")) {
					file = dir->get_next();
					continue;
				}

				String full_path = current_dir.path_join(file);
				String res_path = full_path.replace(project_path, "res:/");

				if (dir->current_is_dir()) {
					if (file != "addons" && file != ".godot" && file != ".git") {
						dirs_to_scan.push_back(full_path);
					}
				} else {
					String ext = file.get_extension().to_lower();
					bool valid_ext = false;
					for (const String &e : extensions) {
						if (ext == e) {
							valid_ext = true;
							break;
						}
					}

					if (valid_ext) {
						if (filter_lower.is_empty() || file.to_lower().contains(filter_lower) || res_path.to_lower().contains(filter_lower)) {
							MentionItem item;
							item.path = res_path;
							item.display = file;
							items.push_back(item);
						}
					}
				}
				file = dir->get_next();
			}
			dir->list_dir_end();
		}
	}

	// 2. Add scene tree nodes from current edited scene
	if (EditorNode::get_singleton() && items.size() < MAX_ITEMS) {
		if (Node *scene_root = EditorNode::get_singleton()->get_edited_scene()) {
			Vector<Node *> nodes;
			nodes.push_back(scene_root);

			while (nodes.size() > 0 && items.size() < MAX_ITEMS) {
				Node *n = nodes[nodes.size() - 1];
				nodes.remove_at(nodes.size() - 1);

				String node_name = n->get_name();
				String node_path = String(scene_root->get_path_to(n));
				if (node_path.is_empty()) {
					node_path = ".";
				}
				String name_lower = node_name.to_lower();

				if (filter_lower.is_empty() || name_lower.contains(filter_lower)) {
					MentionItem item;
					item.path = "node:" + node_path;
					item.display = String::utf8("📦 ") + node_name;
					items.push_back(item);
				}

				for (int i = 0; i < n->get_child_count(); i++) {
					nodes.push_back(n->get_child(i));
				}
			}
		}
	}
}
