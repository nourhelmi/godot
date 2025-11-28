// Gameable Editor Plugin: Chat, Context Panel, and Harness Panel
// Phase 2: Full context management and verification UI
#include "register_types.h"
#include <functional>

#ifdef TOOLS_ENABLED

#include "core/config/project_settings.h"
#include "core/io/json.h"
#include "core/io/resource_loader.h"
#include "core/os/os.h"
#include "core/string/print_string.h"
#include "core/templates/list.h"
#include "editor/docks/editor_dock_manager.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/plugins/editor_plugin.h"
#include "editor/settings/editor_settings.h"
#include "modules/websocket/websocket_peer.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/control.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/item_list.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/option_button.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/rich_text_label.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/separator.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/split_container.h"
#include "scene/gui/tab_container.h"
#include "scene/gui/texture_rect.h"
#include "scene/main/node.h"
#include "scene/main/timer.h"

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Pinned item kinds (mirror agent context.ts)
// ─────────────────────────────────────────────────────────────────────────────
enum PinnedItemKind {
	KIND_SCENE,
	KIND_NODE,
	KIND_SCRIPT,
	KIND_SHADER,
	KIND_MATERIAL,
	KIND_ASSET,
	KIND_MAX
};

static String kind_to_string(PinnedItemKind p_kind) {
	switch (p_kind) {
		case KIND_SCENE:
			return "scene";
		case KIND_NODE:
			return "node";
		case KIND_SCRIPT:
			return "script";
		case KIND_SHADER:
			return "shader";
		case KIND_MATERIAL:
			return "material";
		case KIND_ASSET:
			return "asset";
		default:
			return "asset";
	}
}

static PinnedItemKind string_to_kind(const String &p_str) {
	if (p_str == "scene") {
		return KIND_SCENE;
	}
	if (p_str == "node") {
		return KIND_NODE;
	}
	if (p_str == "script") {
		return KIND_SCRIPT;
	}
	if (p_str == "shader") {
		return KIND_SHADER;
	}
	if (p_str == "material") {
		return KIND_MATERIAL;
	}
	return KIND_ASSET;
}

static String kind_to_icon(PinnedItemKind p_kind) {
	switch (p_kind) {
		case KIND_SCENE:
			return "PackedScene";
		case KIND_NODE:
			return "Node";
		case KIND_SCRIPT:
			return "Script";
		case KIND_SHADER:
			return "Shader";
		case KIND_MATERIAL:
			return "BaseMaterial3D";
		case KIND_ASSET:
			return "File";
		default:
			return "File";
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Context Panel: manages pinned items
// ─────────────────────────────────────────────────────────────────────────────
class ContextPanel final : public VBoxContainer {
	GDCLASS(ContextPanel, VBoxContainer);
	static void _bind_methods() {}

	ItemList *item_list = nullptr;
	OptionButton *bundle_dropdown = nullptr;
	Button *add_selection_btn = nullptr;
	Button *add_file_btn = nullptr;
	Button *remove_btn = nullptr;
	Button *clear_btn = nullptr;
	LineEdit *bundle_name_input = nullptr;
	Button *save_bundle_btn = nullptr;

	// Local state (synced with agent via RPC)
	struct PinnedItem {
		String id;
		PinnedItemKind kind;
		String path;
		String label;
	};
	Vector<PinnedItem> pinned_items;
	Vector<String> bundle_names;

	std::function<void(const String &, const Dictionary &)> send_rpc;

public:
	ContextPanel() {
		set_name("Context");

		// ─── Header: Bundle management ───
		HBoxContainer *bundle_row = memnew(HBoxContainer);
		add_child(bundle_row);

		Label *bundle_label = memnew(Label);
		bundle_label->set_text("Bundle:");
		bundle_row->add_child(bundle_label);

		bundle_dropdown = memnew(OptionButton);
		bundle_dropdown->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		bundle_dropdown->set_tooltip_text("Load a saved context bundle");
		bundle_dropdown->add_item("(none)");
		bundle_row->add_child(bundle_dropdown);
		bundle_dropdown->connect("item_selected", callable_mp(this, &ContextPanel::_on_bundle_selected));

		// Save bundle row
		HBoxContainer *save_row = memnew(HBoxContainer);
		add_child(save_row);

		bundle_name_input = memnew(LineEdit);
		bundle_name_input->set_placeholder("Bundle name...");
		bundle_name_input->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		save_row->add_child(bundle_name_input);

		save_bundle_btn = memnew(Button);
		save_bundle_btn->set_text("Save");
		save_bundle_btn->set_tooltip_text("Save current context as bundle");
		save_row->add_child(save_bundle_btn);
		save_bundle_btn->connect("pressed", callable_mp(this, &ContextPanel::_on_save_bundle));

		add_child(memnew(HSeparator));

		// ─── Toolbar: Add/Remove ───
		HBoxContainer *toolbar = memnew(HBoxContainer);
		add_child(toolbar);

		add_selection_btn = memnew(Button);
		add_selection_btn->set_text("+ Selection");
		add_selection_btn->set_tooltip_text("Pin current editor selection");
		toolbar->add_child(add_selection_btn);
		add_selection_btn->connect("pressed", callable_mp(this, &ContextPanel::_on_add_selection));

		add_file_btn = memnew(Button);
		add_file_btn->set_text("+ File");
		add_file_btn->set_tooltip_text("Pin current file from FileSystem dock");
		toolbar->add_child(add_file_btn);
		add_file_btn->connect("pressed", callable_mp(this, &ContextPanel::_on_add_file));

		toolbar->add_spacer();

		remove_btn = memnew(Button);
		remove_btn->set_text("Remove");
		remove_btn->set_tooltip_text("Remove selected items");
		remove_btn->set_disabled(true);
		toolbar->add_child(remove_btn);
		remove_btn->connect("pressed", callable_mp(this, &ContextPanel::_on_remove_selected));

		clear_btn = memnew(Button);
		clear_btn->set_text("Clear");
		clear_btn->set_tooltip_text("Clear all pinned items");
		toolbar->add_child(clear_btn);
		clear_btn->connect("pressed", callable_mp(this, &ContextPanel::_on_clear_all));

		// ─── Item List ───
		item_list = memnew(ItemList);
		item_list->set_v_size_flags(Control::SIZE_EXPAND_FILL);
		item_list->set_select_mode(ItemList::SELECT_MULTI);
		item_list->set_allow_reselect(true);
		item_list->set_max_columns(1);
		item_list->set_same_column_width(true);
		add_child(item_list);
		item_list->connect("item_selected", callable_mp(this, &ContextPanel::_on_item_selected));
		item_list->connect("item_activated", callable_mp(this, &ContextPanel::_on_item_activated));
		item_list->connect("multi_selected", callable_mp(this, &ContextPanel::_on_multi_selected));

		// ─── Footer: Item count ───
		Label *footer = memnew(Label);
		footer->set_text("0 items pinned");
		footer->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
		footer->add_theme_color_override("font_color", Color(0.6, 0.6, 0.6));
		add_child(footer);
	}

	void set_send_rpc(std::function<void(const String &, const Dictionary &)> p_fn) {
		send_rpc = p_fn;
	}

	void refresh_bundles(const Vector<String> &p_names) {
		bundle_names = p_names;
		bundle_dropdown->clear();
		bundle_dropdown->add_item("(none)");
		for (const String &name : p_names) {
			bundle_dropdown->add_item(name);
		}
	}

	void refresh_items(const Array &p_items) {
		pinned_items.clear();
		item_list->clear();

		Ref<Theme> theme = EditorNode::get_singleton()->get_editor_theme();

		for (int i = 0; i < p_items.size(); i++) {
			Dictionary d = p_items[i];
			PinnedItem item;
			item.id = d.has("id") ? String(d["id"]) : String();
			item.kind = d.has("kind") ? string_to_kind(String(d["kind"])) : KIND_ASSET;
			item.path = d.has("path") ? String(d["path"]) : String();
			item.label = d.has("label") ? String(d["label"]) : item.path.get_file();
			pinned_items.push_back(item);

			// Add to ItemList with icon
			String icon_name = kind_to_icon(item.kind);
			Ref<Texture2D> icon = theme->get_icon(icon_name, "EditorIcons");
			item_list->add_item(item.label.is_empty() ? item.path : item.label, icon);
			item_list->set_item_tooltip(i, item.path);
			item_list->set_item_metadata(i, item.id);
		}

		// Update footer
		if (Label *footer = Object::cast_to<Label>(get_child(get_child_count() - 1))) {
			footer->set_text(itos(pinned_items.size()) + " items pinned");
		}
	}

private:
	void _on_bundle_selected(int p_idx) {
		if (p_idx <= 0 || !send_rpc) {
			return;
		}
		String name = bundle_dropdown->get_item_text(p_idx);
		Dictionary params;
		params["name"] = name;
		send_rpc("bundle.load", params);
	}

	void _on_save_bundle() {
		String name = bundle_name_input->get_text().strip_edges();
		if (name.is_empty() || !send_rpc) {
			return;
		}
		Dictionary params;
		params["name"] = name;
		send_rpc("bundle.save", params);
		bundle_name_input->clear();
	}

	void _on_add_selection() {
		if (!send_rpc) {
			return;
		}
		// Get current selection from editor
		if (EditorSelection *es = EditorNode::get_singleton()->get_editor_selection()) {
			List<Node *> nodes = es->get_full_selected_node_list();
			for (List<Node *>::Element *E = nodes.front(); E; E = E->next()) {
				Node *n = E->get();
				if (!n) {
					continue;
				}
				Dictionary params;
				params["kind"] = "node";
				params["path"] = String(n->get_path());
				params["label"] = n->get_name();
				send_rpc("context.add", params);
			}
		}
	}

	void _on_add_file() {
		if (!send_rpc) {
			return;
		}
		String path = EditorInterface::get_singleton()->get_current_path();
		if (path.is_empty()) {
			return;
		}

		// Determine kind from extension
		PinnedItemKind kind = KIND_ASSET;
		if (path.ends_with(".tscn") || path.ends_with(".scn")) {
			kind = KIND_SCENE;
		} else if (path.ends_with(".gd") || path.ends_with(".cs")) {
			kind = KIND_SCRIPT;
		} else if (path.ends_with(".gdshader") || path.ends_with(".shader")) {
			kind = KIND_SHADER;
		} else if (path.ends_with(".tres") && path.contains("material")) {
			kind = KIND_MATERIAL;
		}

		Dictionary params;
		params["kind"] = kind_to_string(kind);
		params["path"] = path;
		params["label"] = path.get_file();
		send_rpc("context.add", params);
	}

	void _on_remove_selected() {
		if (!send_rpc) {
			return;
		}
		Vector<int> selected = item_list->get_selected_items();
		for (int i = selected.size() - 1; i >= 0; i--) {
			int idx = selected[i];
			String id = item_list->get_item_metadata(idx);
			Dictionary params;
			params["itemId"] = id;
			send_rpc("context.remove", params);
		}
	}

	void _on_clear_all() {
		if (!send_rpc) {
			return;
		}
		Dictionary params;
		send_rpc("context.clear", params);
	}

	void _on_item_selected(int p_idx) {
		remove_btn->set_disabled(item_list->get_selected_items().size() == 0);
	}

	void _on_multi_selected(int p_idx, bool p_selected) {
		remove_btn->set_disabled(item_list->get_selected_items().size() == 0);
	}

	void _on_item_activated(int p_idx) {
		// Open the item in editor
		if (p_idx < 0 || p_idx >= pinned_items.size()) {
			return;
		}
		const PinnedItem &item = pinned_items[p_idx];
		if (item.kind == KIND_SCENE) {
			EditorInterface::get_singleton()->open_scene_from_path(item.path);
		} else if (item.kind == KIND_NODE) {
			// Select node in scene tree
			if (Node *root = EditorNode::get_singleton()->get_edited_scene()) {
				if (Node *n = root->get_node_or_null(item.path)) {
					EditorNode::get_singleton()->get_editor_selection()->clear();
					EditorNode::get_singleton()->get_editor_selection()->add_node(n);
				}
			}
		} else {
			// Open as resource
			Ref<Resource> res = ResourceLoader::load(item.path);
			if (res.is_valid()) {
				EditorInterface::get_singleton()->edit_resource(res);
			} else {
				EditorInterface::get_singleton()->select_file(item.path);
			}
		}
	}
};

// ─────────────────────────────────────────────────────────────────────────────
// Harness Panel: verification and testing
// ─────────────────────────────────────────────────────────────────────────────
class HarnessPanel final : public VBoxContainer {
	GDCLASS(HarnessPanel, VBoxContainer);
	static void _bind_methods() {}

	SpinBox *frames_input = nullptr;
	Button *run_btn = nullptr;
	Label *status_label = nullptr;
	RichTextLabel *logs_view = nullptr;
	TextureRect *screenshot_preview = nullptr;
	VBoxContainer *errors_container = nullptr;
	VBoxContainer *warnings_container = nullptr;

	std::function<void(const String &, const Dictionary &)> send_rpc;

public:
	HarnessPanel() {
		set_name("Harness");

		// ─── Controls Row ───
		HBoxContainer *controls = memnew(HBoxContainer);
		add_child(controls);

		Label *frames_label = memnew(Label);
		frames_label->set_text("Frames:");
		controls->add_child(frames_label);

		frames_input = memnew(SpinBox);
		frames_input->set_min(1);
		frames_input->set_max(600);
		frames_input->set_value(60);
		frames_input->set_tooltip_text("Number of frames to run");
		controls->add_child(frames_input);

		controls->add_spacer();

		run_btn = memnew(Button);
		run_btn->set_text("▶ Run Harness");
		run_btn->set_tooltip_text("Run headless verification on current scene");
		controls->add_child(run_btn);
		run_btn->connect("pressed", callable_mp(this, &HarnessPanel::_on_run_pressed));

		// ─── Status ───
		status_label = memnew(Label);
		status_label->set_text("Ready");
		status_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
		add_child(status_label);

		add_child(memnew(HSeparator));

		// ─── Errors Section ───
		Label *errors_header = memnew(Label);
		errors_header->set_text("Errors");
		errors_header->add_theme_color_override("font_color", Color(1.0, 0.4, 0.4));
		add_child(errors_header);

		errors_container = memnew(VBoxContainer);
		add_child(errors_container);

		// ─── Warnings Section ───
		Label *warnings_header = memnew(Label);
		warnings_header->set_text("Warnings");
		warnings_header->add_theme_color_override("font_color", Color(1.0, 0.8, 0.2));
		add_child(warnings_header);

		warnings_container = memnew(VBoxContainer);
		add_child(warnings_container);

		add_child(memnew(HSeparator));

		// ─── Screenshot Preview ───
		Label *preview_label = memnew(Label);
		preview_label->set_text("Preview");
		add_child(preview_label);

		screenshot_preview = memnew(TextureRect);
		screenshot_preview->set_custom_minimum_size(Size2(200, 120));
		screenshot_preview->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
		screenshot_preview->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
		add_child(screenshot_preview);

		add_child(memnew(HSeparator));

		// ─── Logs View ───
		Label *logs_label = memnew(Label);
		logs_label->set_text("Logs");
		add_child(logs_label);

		logs_view = memnew(RichTextLabel);
		logs_view->set_v_size_flags(Control::SIZE_EXPAND_FILL);
		logs_view->set_autowrap_mode(TextServer::AUTOWRAP_WORD);
		logs_view->set_selection_enabled(true);
		logs_view->add_theme_font_override("normal_font", EditorNode::get_singleton()->get_editor_theme()->get_font("source", "EditorFonts"));
		add_child(logs_view);
	}

	void set_send_rpc(std::function<void(const String &, const Dictionary &)> p_fn) {
		send_rpc = p_fn;
	}

	void show_verify_result(const Dictionary &p_result) {
		// Clear previous
		for (int i = errors_container->get_child_count() - 1; i >= 0; i--) {
			errors_container->get_child(i)->queue_free();
		}
		for (int i = warnings_container->get_child_count() - 1; i >= 0; i--) {
			warnings_container->get_child(i)->queue_free();
		}
		logs_view->clear();

		// Status
		bool ok = p_result.has("ok") ? bool(p_result["ok"]) : false;
		status_label->set_text(ok ? "✓ Passed" : "✗ Failed");
		status_label->add_theme_color_override("font_color", ok ? Color(0.4, 1.0, 0.4) : Color(1.0, 0.4, 0.4));

		// Errors
		if (p_result.has("errors")) {
			Array errors = p_result["errors"];
			for (int i = 0; i < errors.size(); i++) {
				Label *lbl = memnew(Label);
				lbl->set_text("• " + String(errors[i]));
				lbl->add_theme_color_override("font_color", Color(1.0, 0.5, 0.5));
				lbl->set_autowrap_mode(TextServer::AUTOWRAP_WORD);
				errors_container->add_child(lbl);
			}
		}

		// Warnings
		if (p_result.has("warnings")) {
			Array warnings = p_result["warnings"];
			for (int i = 0; i < warnings.size(); i++) {
				Label *lbl = memnew(Label);
				lbl->set_text("• " + String(warnings[i]));
				lbl->add_theme_color_override("font_color", Color(1.0, 0.8, 0.4));
				lbl->set_autowrap_mode(TextServer::AUTOWRAP_WORD);
				warnings_container->add_child(lbl);
			}
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

	void append_log(const String &p_msg) {
		logs_view->append_text(p_msg + "\n");
	}

private:
	void _on_run_pressed() {
		if (!send_rpc) {
			return;
		}
		status_label->set_text("Running...");
		status_label->remove_theme_color_override("font_color");

		// Get current scene path
		String scene_path;
		if (Node *scene_root = EditorNode::get_singleton()->get_edited_scene()) {
			scene_path = scene_root->get_scene_file_path();
		}
		if (scene_path.is_empty()) {
			scene_path = EditorInterface::get_singleton()->get_current_path();
		}
		if (scene_path.is_empty()) {
			status_label->set_text("No scene to run");
			return;
		}

		Dictionary params;
		params["scenePath"] = scene_path;
		params["frames"] = (int)frames_input->get_value();
		send_rpc("runHarness", params);
	}
};

// ─────────────────────────────────────────────────────────────────────────────
// Chat Panel: conversation UI
// ─────────────────────────────────────────────────────────────────────────────
class ChatPanel final : public VBoxContainer {
	GDCLASS(ChatPanel, VBoxContainer);
	static void _bind_methods() {}

	RichTextLabel *log = nullptr;
	LineEdit *input = nullptr;
	Button *send_btn = nullptr;
	Label *token_meter = nullptr;
	VBoxContainer *tool_progress_container = nullptr;
	VBoxContainer *reasoning_section = nullptr;
	Button *reasoning_toggle = nullptr;
	RichTextLabel *reasoning_text = nullptr;

	int64_t turn_tokens = 0;
	int64_t session_tokens = 0;
	int tool_call_count = 0;
	bool reasoning_collapsed = true;
	String current_reasoning;
	HashMap<String, HBoxContainer *> active_tool_rows;
	HashMap<String, uint64_t> tool_result_times;

	std::function<void(const String &, const Dictionary &)> send_rpc;
	std::function<void()> on_new_conversation;

public:
	ChatPanel() {
		set_name("Chat");

		// ─── Token Meter Row ───
		HBoxContainer *meter_row = memnew(HBoxContainer);
		add_child(meter_row);

		token_meter = memnew(Label);
		token_meter->set_text("Tokens: — | Session: —");
		token_meter->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		meter_row->add_child(token_meter);

		Button *new_btn = memnew(Button);
		new_btn->set_text("New");
		new_btn->set_tooltip_text("Start new conversation");
		meter_row->add_child(new_btn);
		new_btn->connect("pressed", callable_mp(this, &ChatPanel::_on_new_conversation));

		// ─── Tool Progress Container ───
		tool_progress_container = memnew(VBoxContainer);
		add_child(tool_progress_container);

		// ─── Chat Log ───
		log = memnew(RichTextLabel);
		log->set_autowrap_mode(TextServer::AUTOWRAP_WORD);
		log->set_v_size_flags(Control::SIZE_EXPAND_FILL);
		log->set_meta_underline(true);
		log->set_selection_enabled(true);
		add_child(log);
		log->connect("meta_clicked", callable_mp(this, &ChatPanel::_on_meta_clicked));

		// ─── Reasoning Section (collapsible) ───
		reasoning_section = memnew(VBoxContainer);
		add_child(reasoning_section);

		reasoning_toggle = memnew(Button);
		reasoning_toggle->set_text("▶ Reasoning");
		reasoning_toggle->set_text_alignment(HORIZONTAL_ALIGNMENT_LEFT);
		reasoning_section->add_child(reasoning_toggle);
		reasoning_toggle->connect("pressed", callable_mp(this, &ChatPanel::_toggle_reasoning));

		reasoning_text = memnew(RichTextLabel);
		reasoning_text->set_autowrap_mode(TextServer::AUTOWRAP_WORD);
		reasoning_text->set_custom_minimum_size(Size2(0, 0));
		reasoning_text->set_v_size_flags(Control::SIZE_SHRINK_BEGIN);
		reasoning_text->set_visible(false);
		reasoning_text->add_theme_font_override("normal_font", EditorNode::get_singleton()->get_editor_theme()->get_font("source", "EditorFonts"));
		reasoning_section->add_child(reasoning_text);

		// ─── Input Row ───
		HBoxContainer *input_row = memnew(HBoxContainer);
		add_child(input_row);

		input = memnew(LineEdit);
		input->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		input->set_placeholder("Ask Gameable...");
		input_row->add_child(input);

		send_btn = memnew(Button);
		send_btn->set_text("Send");
		input_row->add_child(send_btn);

		send_btn->connect("pressed", callable_mp(this, &ChatPanel::_on_send));
		input->connect("text_submitted", callable_mp(this, &ChatPanel::_on_submit));
	}

	void set_send_rpc(std::function<void(const String &, const Dictionary &)> p_fn) {
		send_rpc = p_fn;
	}

	void set_on_new_conversation(std::function<void()> p_fn) {
		on_new_conversation = p_fn;
	}

	void append_message(const String &p_role, const String &p_text) {
		log->append_text("[b]" + p_role + ":[/b] " + p_text + "\n");
	}

	void append_raw(const String &p_text) {
		log->append_text(p_text);
	}

	void append_thinking(const String &p_text) {
		current_reasoning += p_text;
		reasoning_text->append_text(p_text);
	}

	void update_usage(int64_t p_turn, int64_t p_session) {
		turn_tokens = p_turn;
		session_tokens = p_session;
		_update_meter();
	}

	void add_tool_progress(const String &p_id, const String &p_name, const String &p_stage) {
		HBoxContainer *row = nullptr;
		if (active_tool_rows.has(p_id)) {
			row = active_tool_rows[p_id];
			for (int i = 0; i < row->get_child_count(); i++) {
				if (Label *lbl = Object::cast_to<Label>(row->get_child(i))) {
					lbl->set_text(p_name + ": " + p_stage);
					break;
				}
			}
		} else {
			row = memnew(HBoxContainer);
			Label *lbl = memnew(Label);
			lbl->set_text(p_name + ": " + p_stage);
			lbl->set_h_size_flags(Control::SIZE_EXPAND_FILL);
			row->add_child(lbl);
			tool_progress_container->add_child(row);
			active_tool_rows[p_id] = row;
		}
	}

	void mark_tool_done(const String &p_id) {
		tool_result_times[p_id] = OS::get_singleton()->get_ticks_msec();
	}

	void increment_tool_count() {
		tool_call_count++;
		_update_meter();
	}

	void cleanup_done_tools() {
		uint64_t now = OS::get_singleton()->get_ticks_msec();
		Vector<String> to_remove;
		for (const KeyValue<String, uint64_t> &kv : tool_result_times) {
			if (now - kv.value > 1000) {
				to_remove.push_back(kv.key);
			}
		}
		for (const String &id : to_remove) {
			if (active_tool_rows.has(id)) {
				active_tool_rows[id]->queue_free();
				active_tool_rows.erase(id);
			}
			tool_result_times.erase(id);
		}
	}

	void reset_turn_state() {
		turn_tokens = 0;
		tool_call_count = 0;
		current_reasoning = "";
		reasoning_text->clear();
		_update_meter();
	}

	void clear_all() {
		log->clear();
		reasoning_text->clear();
		current_reasoning = "";
		turn_tokens = 0;
		tool_call_count = 0;
		_update_meter();
		for (KeyValue<String, HBoxContainer *> &kv : active_tool_rows) {
			kv.value->queue_free();
		}
		active_tool_rows.clear();
		tool_result_times.clear();
	}

private:
	void _update_meter() {
		String text = "Turn: " + itos(turn_tokens) + " | Session: " + itos(session_tokens);
		if (tool_call_count > 0) {
			text += " | Tools: " + itos(tool_call_count);
		}
		token_meter->set_text(text);
	}

	void _toggle_reasoning() {
		reasoning_collapsed = !reasoning_collapsed;
		reasoning_text->set_visible(!reasoning_collapsed);
		reasoning_toggle->set_text(reasoning_collapsed ? "▶ Reasoning" : "▼ Reasoning");
		if (!reasoning_collapsed) {
			reasoning_text->set_custom_minimum_size(Size2(0, MIN(240.0, reasoning_text->get_content_height())));
		}
	}

	void _on_new_conversation() {
		clear_all();
		if (on_new_conversation) {
			on_new_conversation();
		}
	}

	void _on_send() {
		_submit_input();
	}

	void _on_submit(const String &p_text) {
		_submit_input();
	}

	void _submit_input() {
		String text = input->get_text().strip_edges();
		if (text.is_empty() || !send_rpc) {
			return;
		}
		reset_turn_state();
		append_message("You", text);
		Dictionary params;
		params["prompt"] = text;
		send_rpc("chat", params);
		input->clear();
	}

	void _on_meta_clicked(const Variant &p_meta) {
		if (p_meta.get_type() != Variant::STRING) {
			return;
		}
		String path = p_meta;
		if (path.is_empty()) {
			return;
		}
		if (path.ends_with(".tscn")) {
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
};

// ─────────────────────────────────────────────────────────────────────────────
// Main Dock: TabContainer with Chat, Context, Harness
// ─────────────────────────────────────────────────────────────────────────────
class GameableDock final : public PanelContainer {
	GDCLASS(GameableDock, PanelContainer);
	static void _bind_methods() {}

	TabContainer *tabs = nullptr;
	ChatPanel *chat_panel = nullptr;
	ContextPanel *context_panel = nullptr;
	HarnessPanel *harness_panel = nullptr;

	// Status bar
	HBoxContainer *status_bar = nullptr;
	Label *status_label = nullptr;
	Button *reconnect_btn = nullptr;

	// WebSocket
	WebSocketPeer *ws = nullptr;
	String ws_url;
	Timer *poll_timer = nullptr;
	double retry_delay = 0.5;
	uint64_t next_retry_msec = 0;
	bool sent_hello = false;
	bool sent_context = false;
	uint64_t last_context_sent_msec = 0;
	String last_context_fingerprint;
	int last_ready_state = -1;

	RichTextLabel *bottom_logs = nullptr;

public:
	GameableDock() {
		set_name("Gameable");
		set_process(true);

		VBoxContainer *root = memnew(VBoxContainer);
		add_child(root);

		// ─── Tabs ───
		tabs = memnew(TabContainer);
		tabs->set_v_size_flags(Control::SIZE_EXPAND_FILL);
		root->add_child(tabs);

		chat_panel = memnew(ChatPanel);
		tabs->add_child(chat_panel);

		context_panel = memnew(ContextPanel);
		tabs->add_child(context_panel);

		harness_panel = memnew(HarnessPanel);
		tabs->add_child(harness_panel);

		// Wire up RPC senders
		auto rpc_sender = [this](const String &method, const Dictionary &params) {
			_send_jsonrpc(method, params);
		};
		chat_panel->set_send_rpc(rpc_sender);
		context_panel->set_send_rpc(rpc_sender);
		harness_panel->set_send_rpc(rpc_sender);

		chat_panel->set_on_new_conversation([this]() {
			Dictionary params;
			_send_jsonrpc("clearSession", params);
		});

		// ─── Status Bar ───
		status_bar = memnew(HBoxContainer);
		root->add_child(status_bar);

		status_label = memnew(Label);
		status_label->set_text("Disconnected");
		status_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		status_bar->add_child(status_label);

		reconnect_btn = memnew(Button);
		reconnect_btn->set_text("Reconnect");
		status_bar->add_child(reconnect_btn);
		reconnect_btn->connect("pressed", callable_mp(this, &GameableDock::_reconnect));

		// ─── WebSocket Setup ───
		ws_url = EDITOR_GET("gameable/ws_url");
		_reconnect();

		poll_timer = memnew(Timer);
		poll_timer->set_wait_time(0.1);
		poll_timer->set_one_shot(false);
		poll_timer->set_autostart(true);
		add_child(poll_timer);
		poll_timer->connect("timeout", callable_mp(this, &GameableDock::_on_poll));

		// Listen to selection changes
		if (EditorSelection *es = EditorNode::get_singleton()->get_editor_selection()) {
			es->connect("selection_changed", callable_mp(this, &GameableDock::_on_selection_changed));
		}
	}

	void _process(double p_delta) {
		if (ws && ws->get_ready_state() != WebSocketPeer::STATE_OPEN) {
			ws->poll();
		}
	}

	void set_bottom_logs(RichTextLabel *p_logs) {
		bottom_logs = p_logs;
	}

private:
	void _send_jsonrpc(const String &p_method, const Dictionary &p_params) {
		if (!ws || ws->get_ready_state() != WebSocketPeer::STATE_OPEN) {
			return;
		}
		Dictionary req;
		req["jsonrpc"] = "2.0";
		req["id"] = (int)OS::get_singleton()->get_ticks_msec();
		req["method"] = p_method;
		req["params"] = p_params;
		ws->send_text(JSON::stringify(req));
	}

	void _log_output(const String &p_msg) {
		print_line("[Gameable] ", p_msg);
		if (bottom_logs) {
			bottom_logs->append_text(p_msg + "\n");
		}
	}

	void _reconnect() {
		if (ws) {
			ws->close();
			memdelete(ws);
			ws = nullptr;
		}
		ws = WebSocketPeer::create();
		status_label->set_text("Connecting...");
		_log_output(String("WS connecting to ") + ws_url);
		if (!ws) {
			status_label->set_text("WebSocket unsupported");
			return;
		}
		Error err = ws->connect_to_url(ws_url);
		if (err != OK) {
			status_label->set_text("Connect error: " + itos(err));
			_log_output(String("WS connect error: ") + itos(err));
			retry_delay = MIN(retry_delay * 2.0, 5.0);
			next_retry_msec = OS::get_singleton()->get_ticks_msec() + uint64_t(retry_delay * 1000.0);
		} else {
			retry_delay = 0.5;
		}
		sent_hello = false;
		sent_context = false;
	}

	void _on_selection_changed() {
		if (ws && ws->get_ready_state() == WebSocketPeer::STATE_OPEN) {
			_send_context_snapshot();
		}
	}

	bool _send_context_snapshot(bool p_force = false) {
		Dictionary params;
		params["projectRoot"] = ProjectSettings::get_singleton()->get_resource_path();
		if (Node *scene_root = EditorNode::get_singleton()->get_edited_scene()) {
			String scene_path = scene_root->get_scene_file_path();
			if (!scene_path.is_empty()) {
				params["scenePath"] = scene_path;
			}
		}
		String current_path = EditorInterface::get_singleton()->get_current_path();
		if (!current_path.is_empty()) {
			params["currentPath"] = current_path;
		}
		PackedStringArray sel;
		if (EditorSelection *es = EditorNode::get_singleton()->get_editor_selection()) {
			List<Node *> nodes = es->get_full_selected_node_list();
			for (List<Node *>::Element *E = nodes.front(); E; E = E->next()) {
				Node *n = E->get();
				if (n) {
					sel.push_back(String(n->get_path()));
				}
			}
		}
		if (sel.size() > 0) {
			params["selection"] = sel;
		}

		String fingerprint;
		fingerprint += String(params.has("projectRoot") ? String(params["projectRoot"]) : String());
		fingerprint += String(":") + String(params.has("scenePath") ? String(params["scenePath"]) : String());
		fingerprint += String(":") + String(params.has("currentPath") ? String(params["currentPath"]) : String());
		if (params.has("selection")) {
			PackedStringArray s = params["selection"];
			for (int i = 0; i < s.size(); i++) {
				fingerprint += String(":") + s[i];
			}
		}
		if (!p_force && fingerprint == last_context_fingerprint) {
			return false;
		}
		_send_jsonrpc("context", params);
		sent_context = true;
		last_context_sent_msec = OS::get_singleton()->get_ticks_msec();
		last_context_fingerprint = fingerprint;
		return true;
	}

	void _on_poll() {
		if (!ws) {
			return;
		}
		ws->poll();
		int rs = ws->get_ready_state();

		if (rs != last_ready_state) {
			last_ready_state = rs;
			String rs_text;
			switch (rs) {
				case WebSocketPeer::STATE_CONNECTING:
					rs_text = "Connecting";
					break;
				case WebSocketPeer::STATE_OPEN:
					rs_text = "Connected";
					break;
				case WebSocketPeer::STATE_CLOSING:
					rs_text = "Closing";
					break;
				case WebSocketPeer::STATE_CLOSED:
					rs_text = "Disconnected";
					break;
			}
			status_label->set_text(rs_text);
		}

		switch (rs) {
			case WebSocketPeer::STATE_OPEN: {
				if (!sent_hello) {
					Dictionary hello;
					hello["session"] = "dev";
					_send_jsonrpc("hello", hello);
					sent_hello = true;

					// Fetch bundles on connect
					Dictionary bundle_params;
					_send_jsonrpc("bundle.list", bundle_params);

					// Fetch current context
					Dictionary ctx_params;
					_send_jsonrpc("context.get", ctx_params);
				}
				if (!sent_context) {
					_send_context_snapshot(true);
				}

				// Process incoming messages
				while (ws->get_available_packet_count() > 0) {
					const uint8_t *buf = nullptr;
					int len = 0;
					if (ws->get_packet(&buf, len) == OK && buf && len > 0) {
						String text = ws->was_string_packet() ? String::utf8((const char *)buf, len) : String();
						if (!text.is_empty()) {
							_handle_message(text);
						}
					}
				}

				// Periodic context refresh
				uint64_t now = OS::get_singleton()->get_ticks_msec();
				if (now - last_context_sent_msec > 1500) {
					_send_context_snapshot();
				}

				// Cleanup tool progress
				chat_panel->cleanup_done_tools();
			} break;

			case WebSocketPeer::STATE_CLOSED: {
				uint64_t now = OS::get_singleton()->get_ticks_msec();
				if (now >= next_retry_msec) {
					_reconnect();
				}
			} break;
		}
	}

	void _handle_message(const String &p_text) {
		Variant parsed = JSON::parse_string(p_text);
		if (parsed.get_type() != Variant::DICTIONARY) {
			return;
		}
		Dictionary d = parsed;

		// Handle JSON-RPC responses
		if (d.has("result") && d.has("id")) {
			Dictionary result = d["result"];
			// Bundle list response
			if (result.has("bundles")) {
				Array bundles = result["bundles"];
				Vector<String> names;
				for (int i = 0; i < bundles.size(); i++) {
					names.push_back(bundles[i]);
				}
				context_panel->refresh_bundles(names);
			}
			// Context get response
			if (result.has("items")) {
				context_panel->refresh_items(result["items"]);
			}
			return;
		}

		// Handle notifications
		if (!d.has("method") || d.has("id") || !d.has("params")) {
			return;
		}
		String method = d["method"];
		Dictionary params = d["params"];

		if (method == "status") {
			String level = params.has("level") ? String(params["level"]) : "info";
			String msg = params.has("message") ? String(params["message"]) : String();
			if (msg == "chat:start") {
				chat_panel->reset_turn_state();
			}
			_log_output("[" + level + "] " + msg);
			return;
		}

		if (method == "usage") {
			int64_t turn = 0, session = 0;
			if (params.has("turn")) {
				Dictionary t = params["turn"];
				turn = t.has("totalTokens") ? int64_t(t["totalTokens"]) : 0;
			}
			if (params.has("session")) {
				Dictionary s = params["session"];
				session = s.has("totalTokens") ? int64_t(s["totalTokens"]) : 0;
			}
			chat_panel->update_usage(turn, session);
			return;
		}

		if (method == "thinking") {
			String text = params.has("text") ? String(params["text"]) : String();
			chat_panel->append_thinking(text);
			return;
		}

		if (method == "tool-call") {
			String id = params.has("id") ? String(params["id"]) : String();
			String name = params.has("name") ? String(params["name"]) : String();
			chat_panel->increment_tool_count();
			chat_panel->add_tool_progress(id, name, "running");
			chat_panel->append_raw("[b]" + name + "[/b] ...\n");
			return;
		}

		if (method == "tool-progress") {
			String id = params.has("id") ? String(params["id"]) : String();
			String stage = params.has("stage") ? String(params["stage"]) : String();
			chat_panel->add_tool_progress(id, stage, stage);
			return;
		}

		if (method == "tool-result") {
			String id = params.has("id") ? String(params["id"]) : String();
			chat_panel->mark_tool_done(id);

			// Render search results with clickable links
			if (params.has("name") && String(params["name"]) == "searchFiles" && params.has("output")) {
				Dictionary out = params["output"];
				if (out.has("hits")) {
					Array hits = out["hits"];
					for (int i = 0; i < hits.size(); i++) {
						Dictionary h = hits[i];
						if (h.has("path")) {
							String path = h["path"];
							String preview = h.has("preview") ? String(h["preview"]) : String();
							chat_panel->append_raw(" • [url=" + path + "]" + path + "[/url] — " + preview + "\n");
						}
					}
					return;
				}
			}
			chat_panel->append_raw("[b]result:[/b] " + JSON::stringify(params) + "\n");
			return;
		}

		if (method == "diff-preview") {
			chat_panel->append_raw("[b]diff-preview:[/b] " + JSON::stringify(params) + "\n");
			return;
		}

		if (method == "verify-result") {
			harness_panel->show_verify_result(params);
			// Also switch to harness tab on verify-result
			tabs->set_current_tab(2);
			return;
		}

		// Unknown notification
		chat_panel->append_raw("[agent] " + p_text + "\n");
	}
};

// ─────────────────────────────────────────────────────────────────────────────
// Editor Plugin Registration
// ─────────────────────────────────────────────────────────────────────────────
class GameableEditorBuiltin final : public EditorPlugin {
	GDCLASS(GameableEditorBuiltin, EditorPlugin);
	static void _bind_methods() {}

	Control *chat_dock = nullptr;
	Control *bottom_logs = nullptr;
	Button *bottom_toggle_btn = nullptr;

	void _ensure_dock_first() {
		if (!chat_dock) {
			return;
		}
		Node *parent = chat_dock->get_parent();
		while (parent && Object::cast_to<TabContainer>(parent) == nullptr) {
			parent = parent->get_parent();
		}
		if (TabContainer *tabs = Object::cast_to<TabContainer>(parent)) {
			if (tabs->get_child_count() > 0 && tabs->get_child(0) != chat_dock) {
				tabs->move_child(chat_dock, 0);
			}
		}
	}

public:
	GameableEditorBuiltin() = default;
	~GameableEditorBuiltin() override = default;

	void _notification(int p_what) {
		switch (p_what) {
			case NOTIFICATION_ENTER_TREE: {
				EDITOR_DEF("gameable/enable", true);
				EDITOR_DEF("gameable/ws_url", "ws://127.0.0.1:1999/session/dev");
				if (!bool(EDITOR_GET("gameable/enable"))) {
					return;
				}
				chat_dock = memnew(GameableDock);
				add_control_to_dock(DOCK_SLOT_RIGHT_UL, chat_dock);
				_ensure_dock_first();
				EditorDockManager::get_singleton()->focus_dock(chat_dock);

				// Bottom panel logs
				RichTextLabel *logs = memnew(RichTextLabel);
				logs->set_autowrap_mode(TextServer::AUTOWRAP_WORD);
				logs->set_v_size_flags(Control::SIZE_EXPAND_FILL);
				logs->set_selection_enabled(true);
				bottom_logs = logs;
				bottom_toggle_btn = add_control_to_bottom_panel(bottom_logs, "Gameable");
				if (GameableDock *dock = Object::cast_to<GameableDock>(chat_dock)) {
					dock->set_bottom_logs(Object::cast_to<RichTextLabel>(bottom_logs));
				}
			} break;
			case NOTIFICATION_EXIT_TREE: {
				if (chat_dock) {
					remove_control_from_docks(chat_dock);
					chat_dock->queue_free();
				}
				if (bottom_logs) {
					remove_control_from_bottom_panel(bottom_logs);
					bottom_logs->queue_free();
				}
				chat_dock = nullptr;
				bottom_logs = nullptr;
				bottom_toggle_btn = nullptr;
			} break;
		}
	}
};

static void editor_init_callback() {
	EditorNode::get_singleton()->add_editor_plugin(memnew(GameableEditorBuiltin));
}

} // namespace

#endif // TOOLS_ENABLED

void initialize_gameable_editor_plugin() {
#ifdef TOOLS_ENABLED
	EditorNode::add_init_callback(editor_init_callback);
#endif
}

void uninitialize_gameable_editor_plugin() {}
