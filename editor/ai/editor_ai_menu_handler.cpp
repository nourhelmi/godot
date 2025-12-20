/**************************************************************************/
/*  editor_ai_menu_handler.cpp                                            */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GAMEABLE                                   */
/*                     AI-powered game development                        */
/**************************************************************************/

#include "editor_ai_menu_handler.h"

#include "editor/ai/editor_ai_agent.h"
#include "editor/ai/editor_ai_types.h"

void EditorAIMenuHandler::_bind_methods() {
	// No exposed methods
}

EditorAIMenuHandler::EditorAIMenuHandler() {
}

void EditorAIMenuHandler::get_options(const Vector<String> &p_paths) {
	if (p_paths.is_empty()) {
		return;
	}

	// Add "Add to AI Context" at the top of the menu
	add_context_menu_item(
			"Add to AI Context",
			callable_mp(this, &EditorAIMenuHandler::_on_add_to_context),
			Ref<Texture2D>());
}

void EditorAIMenuHandler::_on_add_to_context(const Variant &p_arg) {
	EditorAIAgent *agent = EditorAIAgent::get_singleton();
	if (!agent) {
		return;
	}

	// p_arg is the paths array
	if (p_arg.get_type() == Variant::PACKED_STRING_ARRAY) {
		PackedStringArray paths = p_arg;
		for (int i = 0; i < paths.size(); i++) {
			String path = paths[i];
			AIContextItemKind kind = ai_context_kind_from_path(path);
			agent->add_context_item(kind, path, path.get_file());
		}
	} else if (p_arg.get_type() == Variant::STRING) {
		String path = p_arg;
		AIContextItemKind kind = ai_context_kind_from_path(path);
		agent->add_context_item(kind, path, path.get_file());
	}
}

