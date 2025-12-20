/**************************************************************************/
/*  editor_ai_agent.h                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GAMEABLE                                   */
/*                     AI-powered game development                        */
/**************************************************************************/

#pragma once

#include "core/object/class_db.h"
#include "core/object/object.h"
#include "core/templates/hash_map.h"
#include "editor/ai/editor_ai_types.h"

class Timer;
class WebSocketPeer;

// Singleton AI agent: WebSocket connection, RPC routing, state management.
// Any editor code can call EditorAIAgent::get_singleton()->request_*()
class EditorAIAgent : public Object {
	GDCLASS(EditorAIAgent, Object);

	static EditorAIAgent *singleton;

	// WebSocket connection
	WebSocketPeer *ws = nullptr;
	String ws_url;
	Timer *poll_timer = nullptr;

	// Connection state
	AIConnectionState connection_state = AI_CONNECTION_DISCONNECTED;
	bool sent_hello = false;
	bool sent_initial_context = false;
	bool selection_connected = false; // Track if we've connected to EditorSelection
	double retry_delay = 0.5;
	uint64_t next_retry_msec = 0;

	// Context tracking (deduplication)
	uint64_t last_context_sent_msec = 0;
	String last_context_fingerprint;

	// Request tracking
	HashMap<int, Callable> pending_requests;
	int next_request_id = 1;

	// Session state
	AITokenUsage session_usage;
	Vector<AIContextItem> pinned_items;
	Vector<String> bundle_names;

	// Internal methods
	void _reconnect();
	void _on_poll();
	void _handle_message(const String &p_text);
	void _handle_response(const Dictionary &p_response);
	void _handle_notification(const String &p_method, const Dictionary &p_params);
	void _handle_file_written(const String &p_tool_name, const Dictionary &p_output);
	bool _send_context_snapshot(bool p_force = false);
	void _on_selection_changed();

protected:
	static void _bind_methods();

public:
	static EditorAIAgent *get_singleton() { return singleton; }
	static void create_singleton();
	static void destroy_singleton();

	// Connection management
	void connect_to_server();
	void disconnect_from_server();
	bool is_agent_connected() const;
	AIConnectionState get_connection_state() const { return connection_state; }

	// Low-level RPC
	void send_jsonrpc(const String &p_method, const Dictionary &p_params);
	int send_jsonrpc_with_callback(const String &p_method, const Dictionary &p_params, const Callable &p_callback);

	// High-level requests (fire and emit signals)
	void request_chat(const String &p_prompt);
	void clear_session();

	// Context management
	void add_context_item(AIContextItemKind p_kind, const String &p_path, const String &p_label = String());
	void remove_context_item(const String &p_item_id);
	void clear_context();
	void load_bundle(const String &p_name);
	void save_bundle(const String &p_name);
	void refresh_bundles();
	void refresh_context();
	const Vector<AIContextItem> &get_pinned_items() const { return pinned_items; }
	const Vector<String> &get_bundle_names() const { return bundle_names; }

	// Harness
	void run_harness(const String &p_scene_path, int p_frames = 60);

	// Usage
	const AITokenUsage &get_session_usage() const { return session_usage; }

	EditorAIAgent();
	~EditorAIAgent();
};
