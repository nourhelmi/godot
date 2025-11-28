/**************************************************************************/
/*  editor_ai_types.h                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GAMEABLE                                   */
/*                     AI-powered game development                        */
/**************************************************************************/

#pragma once

#include "core/string/ustring.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

// Pinned item kinds for context management (mirrors backend context.ts)
enum AIContextItemKind {
	AI_CONTEXT_SCENE,
	AI_CONTEXT_NODE,
	AI_CONTEXT_SCRIPT,
	AI_CONTEXT_SHADER,
	AI_CONTEXT_MATERIAL,
	AI_CONTEXT_ASSET,
	AI_CONTEXT_MAX
};

// Convert kind enum to string for RPC
inline String ai_context_kind_to_string(AIContextItemKind p_kind) {
	switch (p_kind) {
		case AI_CONTEXT_SCENE:
			return "scene";
		case AI_CONTEXT_NODE:
			return "node";
		case AI_CONTEXT_SCRIPT:
			return "script";
		case AI_CONTEXT_SHADER:
			return "shader";
		case AI_CONTEXT_MATERIAL:
			return "material";
		case AI_CONTEXT_ASSET:
		default:
			return "asset";
	}
}

// Convert string to kind enum
inline AIContextItemKind ai_context_string_to_kind(const String &p_str) {
	if (p_str == "scene") {
		return AI_CONTEXT_SCENE;
	}
	if (p_str == "node") {
		return AI_CONTEXT_NODE;
	}
	if (p_str == "script") {
		return AI_CONTEXT_SCRIPT;
	}
	if (p_str == "shader") {
		return AI_CONTEXT_SHADER;
	}
	if (p_str == "material") {
		return AI_CONTEXT_MATERIAL;
	}
	return AI_CONTEXT_ASSET;
}

// Map kind to editor icon name
inline String ai_context_kind_to_icon(AIContextItemKind p_kind) {
	switch (p_kind) {
		case AI_CONTEXT_SCENE:
			return "PackedScene";
		case AI_CONTEXT_NODE:
			return "Node";
		case AI_CONTEXT_SCRIPT:
			return "Script";
		case AI_CONTEXT_SHADER:
			return "Shader";
		case AI_CONTEXT_MATERIAL:
			return "BaseMaterial3D";
		case AI_CONTEXT_ASSET:
		default:
			return "File";
	}
}

// Infer kind from file extension
inline AIContextItemKind ai_context_kind_from_path(const String &p_path) {
	if (p_path.ends_with(".tscn") || p_path.ends_with(".scn")) {
		return AI_CONTEXT_SCENE;
	}
	if (p_path.ends_with(".gd") || p_path.ends_with(".cs")) {
		return AI_CONTEXT_SCRIPT;
	}
	if (p_path.ends_with(".gdshader") || p_path.ends_with(".shader")) {
		return AI_CONTEXT_SHADER;
	}
	if (p_path.ends_with(".tres") && p_path.contains("material")) {
		return AI_CONTEXT_MATERIAL;
	}
	return AI_CONTEXT_ASSET;
}

// Pinned context item struct
struct AIContextItem {
	String id;
	AIContextItemKind kind = AI_CONTEXT_ASSET;
	String path;
	String label;

	Dictionary to_dict() const {
		Dictionary d;
		d["id"] = id;
		d["kind"] = ai_context_kind_to_string(kind);
		d["path"] = path;
		d["label"] = label;
		return d;
	}

	static AIContextItem from_dict(const Dictionary &p_dict) {
		AIContextItem item;
		item.id = p_dict.has("id") ? String(p_dict["id"]) : String();
		item.kind = p_dict.has("kind") ? ai_context_string_to_kind(String(p_dict["kind"])) : AI_CONTEXT_ASSET;
		item.path = p_dict.has("path") ? String(p_dict["path"]) : String();
		item.label = p_dict.has("label") ? String(p_dict["label"]) : item.path.get_file();
		return item;
	}
};

// Token usage tracking
struct AITokenUsage {
	int64_t prompt_tokens = 0;
	int64_t completion_tokens = 0;
	int64_t total_tokens = 0;

	static AITokenUsage from_dict(const Dictionary &p_dict) {
		AITokenUsage usage;
		usage.prompt_tokens = p_dict.has("promptTokens") ? int64_t(p_dict["promptTokens"]) : 0;
		usage.completion_tokens = p_dict.has("completionTokens") ? int64_t(p_dict["completionTokens"]) : 0;
		usage.total_tokens = p_dict.has("totalTokens") ? int64_t(p_dict["totalTokens"]) : 0;
		return usage;
	}
};

// Connection states for UI feedback
enum AIConnectionState {
	AI_CONNECTION_DISCONNECTED,
	AI_CONNECTION_CONNECTING,
	AI_CONNECTION_CONNECTED,
	AI_CONNECTION_CLOSING
};
