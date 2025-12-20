/**************************************************************************/
/*  editor_ai_menu_handler.h                                              */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GAMEABLE                                   */
/*                     AI-powered game development                        */
/**************************************************************************/

#pragma once

#include "editor/inspector/editor_context_menu_plugin.h"

// Context menu handler for AI actions in editor.
// Registers "Add to AI Context" in filesystem dock, scene tree, etc.
class EditorAIMenuHandler : public EditorContextMenuPlugin {
	GDCLASS(EditorAIMenuHandler, EditorContextMenuPlugin);

	void _on_add_to_context(const Variant &p_arg);

protected:
	static void _bind_methods();

public:
	virtual void get_options(const Vector<String> &p_paths) override;

	EditorAIMenuHandler();
};

