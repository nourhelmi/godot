/**************************************************************************/
/*  ai_assets_dock.cpp                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GAMEABLE                                   */
/*                     AI-powered game development                        */
/**************************************************************************/

#include "ai_assets_dock.h"

#include "core/config/project_settings.h"
#include "core/io/image.h"
#include "editor/ai/editor_ai_agent.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/texture_rect.h"
#include "scene/resources/image_texture.h"

void AIAssetsDock::_bind_methods() {
	// No script API currently.
}

void AIAssetsDock::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			if (EditorAIAgent *agent = EditorAIAgent::get_singleton()) {
				Callable listed_cb = callable_mp(this, &AIAssetsDock::_on_assets_listed);
				Callable ready_cb = callable_mp(this, &AIAssetsDock::_on_asset_ready);
				Callable updated_cb = callable_mp(this, &AIAssetsDock::_on_asset_updated);
				Callable imported_cb = callable_mp(this, &AIAssetsDock::_on_asset_imported);
				if (!agent->is_connected("assets_listed", listed_cb)) {
					agent->connect("assets_listed", listed_cb);
				}
				if (!agent->is_connected("asset_ready", ready_cb)) {
					agent->connect("asset_ready", ready_cb);
				}
				if (!agent->is_connected("asset_updated", updated_cb)) {
					agent->connect("asset_updated", updated_cb);
				}
				if (!agent->is_connected("asset_imported", imported_cb)) {
					agent->connect("asset_imported", imported_cb);
				}
			}
			_request_refresh();
		} break;
	}
}

AIAssetsDock::AIAssetsDock() {
	set_name("Assets");
	set_h_size_flags(SIZE_EXPAND_FILL);
	set_v_size_flags(SIZE_EXPAND_FILL);
	_build_ui();
}

AIAssetsDock::~AIAssetsDock() {
}

void AIAssetsDock::_build_ui() {
	add_theme_constant_override("separation", 8 * EDSCALE);

	header_row = memnew(HBoxContainer);
	header_row->add_theme_constant_override("separation", 8 * EDSCALE);
	add_child(header_row);

	summary_label = memnew(Label);
	summary_label->set_text("Staged assets: 0");
	summary_label->set_h_size_flags(SIZE_EXPAND_FILL);
	header_row->add_child(summary_label);

	refresh_btn = memnew(Button);
	refresh_btn->set_text("Refresh");
	refresh_btn->set_flat(true);
	refresh_btn->connect("pressed", callable_mp(this, &AIAssetsDock::_on_refresh_pressed));
	header_row->add_child(refresh_btn);

	generate_row = memnew(HBoxContainer);
	generate_row->set_h_size_flags(SIZE_EXPAND_FILL);
	generate_row->add_theme_constant_override("separation", 8 * EDSCALE);
	add_child(generate_row);

	prompt_input = memnew(LineEdit);
	prompt_input->set_h_size_flags(SIZE_EXPAND_FILL);
	prompt_input->set_placeholder("Describe an asset... (quick generate)");
	generate_row->add_child(prompt_input);

	generate_image_btn = memnew(Button);
	generate_image_btn->set_text("Gen 2D");
	generate_image_btn->connect("pressed", callable_mp(this, &AIAssetsDock::_on_generate_image_pressed));
	generate_row->add_child(generate_image_btn);

	generate_model_btn = memnew(Button);
	generate_model_btn->set_text("Gen 3D");
	generate_model_btn->connect("pressed", callable_mp(this, &AIAssetsDock::_on_generate_model_pressed));
	generate_row->add_child(generate_model_btn);

	scroll = memnew(ScrollContainer);
	scroll->set_h_size_flags(SIZE_EXPAND_FILL);
	scroll->set_v_size_flags(SIZE_EXPAND_FILL);
	add_child(scroll);

	cards_container = memnew(VBoxContainer);
	cards_container->set_h_size_flags(SIZE_EXPAND_FILL);
	cards_container->set_v_size_flags(SIZE_EXPAND_FILL);
	cards_container->add_theme_constant_override("separation", 8 * EDSCALE);
	scroll->add_child(cards_container);
}

void AIAssetsDock::_request_refresh() {
	if (EditorAIAgent *agent = EditorAIAgent::get_singleton()) {
		agent->request_asset_list(true, String(), 300);
	}
}

void AIAssetsDock::_on_refresh_pressed() {
	_request_refresh();
}

void AIAssetsDock::_on_assets_listed(const Array &p_assets) {
	_render_assets(p_assets);
}

void AIAssetsDock::_on_asset_ready(const Dictionary &p_payload) {
	(void)p_payload;
	_request_refresh();
}

void AIAssetsDock::_on_asset_updated(const Dictionary &p_payload) {
	(void)p_payload;
	_request_refresh();
}

void AIAssetsDock::_on_asset_imported(const Dictionary &p_asset) {
	(void)p_asset;
	_request_refresh();
}

void AIAssetsDock::_request_generate(bool p_generate_model) {
	if (!prompt_input) {
		return;
	}
	const String prompt = prompt_input->get_text().strip_edges();
	if (prompt.is_empty()) {
		return;
	}
	if (EditorAIAgent *agent = EditorAIAgent::get_singleton()) {
		const String instruction = p_generate_model
				? String("Generate one staged 3D model asset using generateModel with this prompt, then stop. Prompt: ") + prompt
				: String("Generate one staged 2D image asset using generateImage with this prompt, then stop. Prompt: ") + prompt;
		agent->request_chat(instruction);
	}
	prompt_input->clear();
}

void AIAssetsDock::_on_generate_image_pressed() {
	_request_generate(false);
}

void AIAssetsDock::_on_generate_model_pressed() {
	_request_generate(true);
}

void AIAssetsDock::_on_import_pressed(const String &p_asset_id) {
	if (EditorAIAgent *agent = EditorAIAgent::get_singleton()) {
		agent->request_asset_import(p_asset_id);
	}
}

void AIAssetsDock::_on_discard_pressed(const String &p_asset_id) {
	if (EditorAIAgent *agent = EditorAIAgent::get_singleton()) {
		agent->request_asset_discard(p_asset_id);
	}
}

void AIAssetsDock::_on_tag_pressed(const String &p_asset_id) {
	LineEdit **slot = tag_inputs_by_asset_id.getptr(p_asset_id);
	if (!slot || !*slot) {
		return;
	}
	LineEdit *input = *slot;
	PackedStringArray tags = _parse_tags(input->get_text());
	if (tags.is_empty()) {
		return;
	}
	if (EditorAIAgent *agent = EditorAIAgent::get_singleton()) {
		agent->request_asset_tag(p_asset_id, "add", tags);
	}
	input->clear();
}

void AIAssetsDock::_clear_cards() {
	tag_inputs_by_asset_id.clear();
	if (!cards_container) {
		return;
	}
	while (cards_container->get_child_count() > 0) {
		Node *child = cards_container->get_child(0);
		cards_container->remove_child(child);
		memdelete(child);
	}
}

void AIAssetsDock::_render_assets(const Array &p_assets) {
	_clear_cards();
	if (summary_label) {
		summary_label->set_text("Assets: " + itos(p_assets.size()));
	}
	if (p_assets.is_empty()) {
		Label *empty = memnew(Label);
		empty->set_h_size_flags(SIZE_EXPAND_FILL);
		empty->set_text("No staged assets yet.\nUse the quick prompt above or generate from Chat.");
		cards_container->add_child(empty);
		return;
	}
	for (int i = 0; i < p_assets.size(); i++) {
		if (p_assets[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		Dictionary asset = p_assets[i];
		PanelContainer *card = _create_asset_card(asset);
		if (card) {
			cards_container->add_child(card);
		}
	}
}

PanelContainer *AIAssetsDock::_create_asset_card(const Dictionary &p_asset) {
	String asset_id = p_asset.has("id") ? String(p_asset["id"]) : String();
	String kind = p_asset.has("kind") ? String(p_asset["kind"]) : String("image");
	String status = p_asset.has("status") ? String(p_asset["status"]) : String("staged");
	String prompt = p_asset.has("prompt") ? String(p_asset["prompt"]) : String();
	String stage_path = p_asset.has("stagePath") ? String(p_asset["stagePath"]) : String();
	String suggested_path = p_asset.has("suggestedImportPath") ? String(p_asset["suggestedImportPath"]) : String();
	String imported_path = p_asset.has("importedPath") ? String(p_asset["importedPath"]) : String();
	Array tags = p_asset.has("tags") ? Array(p_asset["tags"]) : Array();

	PanelContainer *card = memnew(PanelContainer);
	card->set_h_size_flags(SIZE_EXPAND_FILL);

	VBoxContainer *content = memnew(VBoxContainer);
	content->add_theme_constant_override("separation", 4 * EDSCALE);
	card->add_child(content);

	Label *title = memnew(Label);
	title->set_text("[" + status + "] " + kind + "  •  " + asset_id);
	content->add_child(title);

	if (!prompt.is_empty()) {
		Label *prompt_label = memnew(Label);
		prompt_label->set_text(prompt);
		content->add_child(prompt_label);
	}

	Label *stage_label = memnew(Label);
	stage_label->set_text("staged: " + stage_path);
	content->add_child(stage_label);

	Label *suggested_label = memnew(Label);
	suggested_label->set_text("import→ " + suggested_path);
	content->add_child(suggested_label);

	if (!imported_path.is_empty()) {
		Label *imported_label = memnew(Label);
		imported_label->set_text("imported: " + imported_path);
		content->add_child(imported_label);
	}

	String tags_text;
	for (int i = 0; i < tags.size(); i++) {
		if (i > 0) {
			tags_text += ", ";
		}
		tags_text += String(tags[i]);
	}
	Label *tags_label = memnew(Label);
	tags_label->set_text(tags_text.is_empty() ? "tags: (none)" : "tags: " + tags_text);
	content->add_child(tags_label);

	// Inline preview for images.
	if (kind == "image") {
		String preview_path = !imported_path.is_empty() ? imported_path : stage_path;
		Ref<Texture2D> preview = _load_image_preview(preview_path);
		if (preview.is_valid()) {
			TextureRect *preview_rect = memnew(TextureRect);
			preview_rect->set_texture(preview);
			preview_rect->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
			preview_rect->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
			preview_rect->set_custom_minimum_size(Size2(220 * EDSCALE, 120 * EDSCALE));
			content->add_child(preview_rect);
		}
	}

	HBoxContainer *actions = memnew(HBoxContainer);
	actions->set_h_size_flags(SIZE_EXPAND_FILL);
	actions->add_theme_constant_override("separation", 6 * EDSCALE);
	content->add_child(actions);

	if (status == "staged") {
		Button *import_btn = memnew(Button);
		import_btn->set_text("Import");
		import_btn->connect("pressed", callable_mp(this, &AIAssetsDock::_on_import_pressed).bind(asset_id));
		actions->add_child(import_btn);

		Button *discard_btn = memnew(Button);
		discard_btn->set_text("Discard");
		discard_btn->set_flat(true);
		discard_btn->connect("pressed", callable_mp(this, &AIAssetsDock::_on_discard_pressed).bind(asset_id));
		actions->add_child(discard_btn);
	}

	LineEdit *tag_input = memnew(LineEdit);
	tag_input->set_h_size_flags(SIZE_EXPAND_FILL);
	tag_input->set_placeholder("tag1, tag2");
	actions->add_child(tag_input);
	tag_inputs_by_asset_id.insert(asset_id, tag_input);

	Button *add_tag_btn = memnew(Button);
	add_tag_btn->set_text("Add Tag");
	add_tag_btn->set_flat(true);
	add_tag_btn->connect("pressed", callable_mp(this, &AIAssetsDock::_on_tag_pressed).bind(asset_id));
	actions->add_child(add_tag_btn);

	return card;
}

Ref<Texture2D> AIAssetsDock::_load_image_preview(const String &p_path) const {
	String load_path = p_path;
	if (load_path.begins_with("res://")) {
		if (ProjectSettings::get_singleton()) {
			load_path = ProjectSettings::get_singleton()->globalize_path(load_path);
		}
	}

	Ref<Image> image;
	image.instantiate();
	if (image->load(load_path) != OK) {
		return Ref<Texture2D>();
	}

	return ImageTexture::create_from_image(image);
}

PackedStringArray AIAssetsDock::_parse_tags(const String &p_text) const {
	PackedStringArray tags;
	PackedStringArray parts = p_text.split(",");
	for (int i = 0; i < parts.size(); i++) {
		String tag = parts[i].strip_edges().to_lower();
		if (tag.is_empty()) {
			continue;
		}
		tags.push_back(tag);
	}
	return tags;
}
