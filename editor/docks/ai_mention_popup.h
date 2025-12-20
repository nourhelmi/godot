/**************************************************************************/
/*  ai_mention_popup.h                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GAMEABLE                                   */
/*                     AI-powered game development                        */
/**************************************************************************/

#pragma once

#include "scene/gui/panel_container.h"

class ItemList;

// @ mention autocomplete popup for chat input.
// Uses PanelContainer + ItemList instead of PopupMenu to avoid focus stealing.
class AIMentionPopup : public PanelContainer {
	GDCLASS(AIMentionPopup, PanelContainer);

	ItemList *item_list = nullptr;
	int mention_start_col = -1;
	String current_filter;

	struct MentionItem {
		String path; // Full path or "node:..." for nodes
		String display; // Display text
	};
	Vector<MentionItem> items;

	void _populate(const String &p_filter);
	void _emit_selected(int p_idx);

protected:
	static void _bind_methods();

public:
	// Call when user types @ - returns true if popup should show
	bool update_filter(const String &p_text_before_caret, int p_caret_col);

	// Position and show the popup relative to anchor control
	void show_at(Control *p_anchor);

	// Handle keyboard input from parent (returns true if consumed)
	bool handle_key(Key p_keycode);

	// Get the column where @ was typed (for text replacement)
	int get_mention_start_col() const { return mention_start_col; }

	// Reset state
	void cancel();

	AIMentionPopup();
};
