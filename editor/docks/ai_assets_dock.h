/**************************************************************************/
/*  ai_assets_dock.h                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GAMEABLE                                   */
/*                     AI-powered game development                        */
/**************************************************************************/

#pragma once

#include "core/templates/hash_map.h"
#include "scene/gui/box_container.h"

class Button;
class Label;
class LineEdit;
class PanelContainer;
class ScrollContainer;
class Texture2D;
class VBoxContainer;

// Assets panel: staged generated assets with inline previews, import button,
// and cross-chat tags. Import path is auto-chosen by asset kind.
class AIAssetsDock : public VBoxContainer {
	GDCLASS(AIAssetsDock, VBoxContainer);

	HBoxContainer *header_row = nullptr;
	Label *summary_label = nullptr;
	Button *refresh_btn = nullptr;
	HBoxContainer *generate_row = nullptr;
	LineEdit *prompt_input = nullptr;
	Button *generate_image_btn = nullptr;
	Button *generate_model_btn = nullptr;

	ScrollContainer *scroll = nullptr;
	VBoxContainer *cards_container = nullptr;

	HashMap<String, LineEdit *> tag_inputs_by_asset_id;

	void _build_ui();
	void _clear_cards();
	void _render_assets(const Array &p_assets);
	PanelContainer *_create_asset_card(const Dictionary &p_asset);
	Ref<Texture2D> _load_image_preview(const String &p_path) const;
	PackedStringArray _parse_tags(const String &p_text) const;
	void _request_refresh();

	void _on_refresh_pressed();
	void _on_assets_listed(const Array &p_assets);
	void _on_asset_ready(const Dictionary &p_payload);
	void _on_asset_updated(const Dictionary &p_payload);
	void _on_asset_imported(const Dictionary &p_asset);
	void _request_generate(bool p_generate_model);
	void _on_generate_image_pressed();
	void _on_generate_model_pressed();
	void _on_import_pressed(const String &p_asset_id);
	void _on_discard_pressed(const String &p_asset_id);
	void _on_tag_pressed(const String &p_asset_id);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	AIAssetsDock();
	~AIAssetsDock();
};
