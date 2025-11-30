/**************************************************************************/
/*  editor_ai_agent.cpp                                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GAMEABLE                                   */
/*                     AI-powered game development                        */
/**************************************************************************/

#include "editor_ai_agent.h"

#include "core/config/project_settings.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "core/string/print_string.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/settings/editor_settings.h"
#include "modules/websocket/websocket_peer.h"
#include "scene/main/timer.h"

EditorAIAgent *EditorAIAgent::singleton = nullptr;

void EditorAIAgent::_bind_methods() {
	// Signals for UI binding
	ADD_SIGNAL(MethodInfo("connection_state_changed", PropertyInfo(Variant::INT, "state")));
	ADD_SIGNAL(MethodInfo("thinking", PropertyInfo(Variant::STRING, "text")));
	ADD_SIGNAL(MethodInfo("status", PropertyInfo(Variant::STRING, "level"), PropertyInfo(Variant::STRING, "message")));
	ADD_SIGNAL(MethodInfo("usage_updated", PropertyInfo(Variant::INT, "turn_tokens"), PropertyInfo(Variant::INT, "session_tokens")));
	ADD_SIGNAL(MethodInfo("tool_call", PropertyInfo(Variant::STRING, "id"), PropertyInfo(Variant::STRING, "name"), PropertyInfo(Variant::DICTIONARY, "input")));
	ADD_SIGNAL(MethodInfo("tool_result", PropertyInfo(Variant::STRING, "id"), PropertyInfo(Variant::BOOL, "ok"), PropertyInfo(Variant::DICTIONARY, "output")));
	ADD_SIGNAL(MethodInfo("tool_progress", PropertyInfo(Variant::STRING, "id"), PropertyInfo(Variant::STRING, "stage"), PropertyInfo(Variant::FLOAT, "progress")));
	ADD_SIGNAL(MethodInfo("verify_result", PropertyInfo(Variant::DICTIONARY, "result")));
	ADD_SIGNAL(MethodInfo("context_updated", PropertyInfo(Variant::ARRAY, "items")));
	ADD_SIGNAL(MethodInfo("bundles_updated", PropertyInfo(Variant::PACKED_STRING_ARRAY, "names")));
	ADD_SIGNAL(MethodInfo("chat_message", PropertyInfo(Variant::STRING, "role"), PropertyInfo(Variant::STRING, "text")));
}

void EditorAIAgent::create_singleton() {
	ERR_FAIL_COND(singleton != nullptr);
	singleton = memnew(EditorAIAgent);
}

void EditorAIAgent::destroy_singleton() {
	if (singleton) {
		memdelete(singleton);
		singleton = nullptr;
	}
}

EditorAIAgent::EditorAIAgent() {
	// Default WS URL - actual setting read deferred to connect_to_server()
	// when EditorSettings is guaranteed to be initialized
	ws_url = "ws://127.0.0.1:1999/session/dev";

	// Create poll timer (not added to tree yet - done in connect_to_server)
	poll_timer = memnew(Timer);
	poll_timer->set_wait_time(0.1);
	poll_timer->set_one_shot(false);
	poll_timer->connect("timeout", callable_mp(this, &EditorAIAgent::_on_poll));
}

EditorAIAgent::~EditorAIAgent() {
	disconnect_from_server();
	if (poll_timer) {
		poll_timer->stop();
		memdelete(poll_timer);
		poll_timer = nullptr;
	}
}

void EditorAIAgent::connect_to_server() {
	// Read WS URL from settings now that EditorSettings is initialized
	if (EditorSettings::get_singleton() && EditorSettings::get_singleton()->has_setting("gameable/ws_url")) {
		String url = EDITOR_GET("gameable/ws_url");
		if (!url.is_empty()) {
			ws_url = url;
		}
	}

	// Connect selection change listener (deferred since EditorNode might be busy)
	if (EditorNode::get_singleton() && !selection_connected) {
		if (EditorSelection *es = EditorNode::get_singleton()->get_editor_selection()) {
			es->connect("selection_changed", callable_mp(this, &EditorAIAgent::_on_selection_changed));
			selection_connected = true;
		}
	}

	_reconnect();

	// Timer needs to be in tree to work; add via deferred call to avoid "parent busy" errors
	if (poll_timer && !poll_timer->is_inside_tree()) {
		if (EditorNode::get_singleton()) {
			EditorNode::get_singleton()->call_deferred("add_child", poll_timer);
			poll_timer->call_deferred("start");
		}
	}
}

void EditorAIAgent::disconnect_from_server() {
	if (ws) {
		ws->close();
		memdelete(ws);
		ws = nullptr;
	}
	connection_state = AI_CONNECTION_DISCONNECTED;
	emit_signal("connection_state_changed", (int)connection_state);
}

bool EditorAIAgent::is_agent_connected() const {
	return ws && ws->get_ready_state() == WebSocketPeer::STATE_OPEN;
}

void EditorAIAgent::_reconnect() {
	if (ws) {
		ws->close();
		memdelete(ws);
		ws = nullptr;
	}

	ws = WebSocketPeer::create();
	if (!ws) {
		connection_state = AI_CONNECTION_DISCONNECTED;
		emit_signal("connection_state_changed", (int)connection_state);
		emit_signal("status", "error", "WebSocket not supported");
		return;
	}

	connection_state = AI_CONNECTION_CONNECTING;
	emit_signal("connection_state_changed", (int)connection_state);

	Error err = ws->connect_to_url(ws_url);
	if (err != OK) {
		connection_state = AI_CONNECTION_DISCONNECTED;
		emit_signal("connection_state_changed", (int)connection_state);
		emit_signal("status", "error", "Connection error: " + itos(err));

		// Exponential backoff
		retry_delay = MIN(retry_delay * 2.0, 5.0);
		next_retry_msec = OS::get_singleton()->get_ticks_msec() + uint64_t(retry_delay * 1000.0);
	} else {
		retry_delay = 0.5;
	}

	sent_hello = false;
	sent_initial_context = false;
}

void EditorAIAgent::_on_poll() {
	if (!ws) {
		return;
	}

	ws->poll();
	int rs = ws->get_ready_state();

	// Track state transitions
	AIConnectionState new_state = connection_state;
	switch (rs) {
		case WebSocketPeer::STATE_CONNECTING:
			new_state = AI_CONNECTION_CONNECTING;
			break;
		case WebSocketPeer::STATE_OPEN:
			new_state = AI_CONNECTION_CONNECTED;
			break;
		case WebSocketPeer::STATE_CLOSING:
			new_state = AI_CONNECTION_CLOSING;
			break;
		case WebSocketPeer::STATE_CLOSED:
			new_state = AI_CONNECTION_DISCONNECTED;
			break;
	}

	if (new_state != connection_state) {
		connection_state = new_state;
		emit_signal("connection_state_changed", (int)connection_state);
	}

	switch (rs) {
		case WebSocketPeer::STATE_OPEN: {
			// Send hello on first connect
			if (!sent_hello) {
				Dictionary hello;
				hello["session"] = "dev";
				send_jsonrpc("hello", hello);
				sent_hello = true;

				// Fetch bundles and context on connect
				refresh_bundles();
				refresh_context();
			}

			// Send initial context snapshot
			if (!sent_initial_context) {
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

			// Periodic context refresh (throttled)
			uint64_t now = OS::get_singleton()->get_ticks_msec();
			if (now - last_context_sent_msec > 1500) {
				_send_context_snapshot();
			}
		} break;

		case WebSocketPeer::STATE_CLOSED: {
			// Auto-reconnect with backoff
			uint64_t now = OS::get_singleton()->get_ticks_msec();
			if (now >= next_retry_msec) {
				_reconnect();
			}
		} break;
	}
}

void EditorAIAgent::_handle_message(const String &p_text) {
	Variant parsed = JSON::parse_string(p_text);
	if (parsed.get_type() != Variant::DICTIONARY) {
		return;
	}

	Dictionary d = parsed;

	// JSON-RPC response (has result and id)
	if (d.has("result") && d.has("id")) {
		_handle_response(d);
		return;
	}

	// JSON-RPC notification (has method, no id)
	if (d.has("method") && !d.has("id") && d.has("params")) {
		String method = d["method"];
		Dictionary params = d["params"];
		_handle_notification(method, params);
		return;
	}
}

void EditorAIAgent::_handle_response(const Dictionary &p_response) {
	int id = p_response.has("id") ? int(p_response["id"]) : 0;
	Dictionary result = p_response.has("result") ? Dictionary(p_response["result"]) : Dictionary();

	// Check for pending callbacks
	if (pending_requests.has(id)) {
		Callable cb = pending_requests[id];
		pending_requests.erase(id);
		if (cb.is_valid()) {
			cb.call(result);
		}
		return;
	}

	// Handle known response types
	if (result.has("bundles")) {
		Array bundles = result["bundles"];
		bundle_names.clear();
		for (int i = 0; i < bundles.size(); i++) {
			bundle_names.push_back(bundles[i]);
		}
		emit_signal("bundles_updated", bundle_names);
	}

	if (result.has("items")) {
		Array items = result["items"];
		pinned_items.clear();
		for (int i = 0; i < items.size(); i++) {
			pinned_items.push_back(AIContextItem::from_dict(items[i]));
		}
		emit_signal("context_updated", items);
	}
}

void EditorAIAgent::_handle_notification(const String &p_method, const Dictionary &p_params) {
	if (p_method == "status") {
		String level = p_params.has("level") ? String(p_params["level"]) : "info";
		String msg = p_params.has("message") ? String(p_params["message"]) : String();
		emit_signal("status", level, msg);
		return;
	}

	if (p_method == "thinking") {
		String text = p_params.has("text") ? String(p_params["text"]) : String();
		emit_signal("thinking", text);
		return;
	}

	if (p_method == "usage") {
		int64_t turn = 0;
		if (p_params.has("turn")) {
			Dictionary t = p_params["turn"];
			turn = t.has("totalTokens") ? int64_t(t["totalTokens"]) : 0;
		}
		if (p_params.has("session")) {
			session_usage = AITokenUsage::from_dict(p_params["session"]);
		}
		emit_signal("usage_updated", turn, session_usage.total_tokens);
		return;
	}

	if (p_method == "tool-call") {
		String id = p_params.has("id") ? String(p_params["id"]) : String();
		String name = p_params.has("name") ? String(p_params["name"]) : String();
		Dictionary input = p_params.has("input") ? Dictionary(p_params["input"]) : Dictionary();
		emit_signal("tool_call", id, name, input);
		return;
	}

	if (p_method == "tool-result") {
		String id = p_params.has("id") ? String(p_params["id"]) : String();
		bool ok = p_params.has("ok") ? bool(p_params["ok"]) : false;
		Dictionary output = p_params.has("output") ? Dictionary(p_params["output"]) : Dictionary();
		emit_signal("tool_result", id, ok, output);
		return;
	}

	if (p_method == "tool-progress") {
		String id = p_params.has("id") ? String(p_params["id"]) : String();
		String stage = p_params.has("stage") ? String(p_params["stage"]) : String();
		float progress = p_params.has("progress") ? float(p_params["progress"]) : -1.0f;
		emit_signal("tool_progress", id, stage, progress);
		return;
	}

	if (p_method == "verify-result") {
		emit_signal("verify_result", p_params);
		return;
	}
}

bool EditorAIAgent::_send_context_snapshot(bool p_force) {
	if (!is_agent_connected()) {
		return false;
	}

	Dictionary params;
	params["projectRoot"] = ProjectSettings::get_singleton()->get_resource_path();

	// Current scene
	if (EditorNode::get_singleton()) {
		if (Node *scene_root = EditorNode::get_singleton()->get_edited_scene()) {
			String scene_path = scene_root->get_scene_file_path();
			if (!scene_path.is_empty()) {
				params["scenePath"] = scene_path;
			}
		}
	}

	// Current path in filesystem
	String current_path = EditorInterface::get_singleton()->get_current_path();
	if (!current_path.is_empty()) {
		params["currentPath"] = current_path;
	}

	// Selected nodes
	PackedStringArray sel;
	if (EditorNode::get_singleton()) {
		if (EditorSelection *es = EditorNode::get_singleton()->get_editor_selection()) {
			List<Node *> nodes = es->get_full_selected_node_list();
			for (List<Node *>::Element *E = nodes.front(); E; E = E->next()) {
				Node *n = E->get();
				if (n) {
					sel.push_back(String(n->get_path()));
				}
			}
		}
	}
	if (sel.size() > 0) {
		params["selection"] = sel;
	}

	// Fingerprint for deduplication
	String fingerprint;
	fingerprint += params.has("projectRoot") ? String(params["projectRoot"]) : String();
	fingerprint += ":" + (params.has("scenePath") ? String(params["scenePath"]) : String());
	fingerprint += ":" + (params.has("currentPath") ? String(params["currentPath"]) : String());
	if (params.has("selection")) {
		PackedStringArray s = params["selection"];
		for (int i = 0; i < s.size(); i++) {
			fingerprint += ":" + s[i];
		}
	}

	if (!p_force && fingerprint == last_context_fingerprint) {
		return false;
	}

	send_jsonrpc("context", params);
	sent_initial_context = true;
	last_context_sent_msec = OS::get_singleton()->get_ticks_msec();
	last_context_fingerprint = fingerprint;
	return true;
}

void EditorAIAgent::_on_selection_changed() {
	_send_context_snapshot();
}

void EditorAIAgent::send_jsonrpc(const String &p_method, const Dictionary &p_params) {
	if (!is_agent_connected()) {
		return;
	}

	Dictionary req;
	req["jsonrpc"] = "2.0";
	req["id"] = next_request_id++;
	req["method"] = p_method;
	req["params"] = p_params;
	ws->send_text(JSON::stringify(req));
}

int EditorAIAgent::send_jsonrpc_with_callback(const String &p_method, const Dictionary &p_params, const Callable &p_callback) {
	if (!is_agent_connected()) {
		return -1;
	}

	int id = next_request_id++;
	Dictionary req;
	req["jsonrpc"] = "2.0";
	req["id"] = id;
	req["method"] = p_method;
	req["params"] = p_params;

	pending_requests[id] = p_callback;
	ws->send_text(JSON::stringify(req));
	return id;
}

void EditorAIAgent::request_chat(const String &p_prompt) {
	Dictionary params;
	params["prompt"] = p_prompt;
	send_jsonrpc("chat", params);
	emit_signal("chat_message", "You", p_prompt);
}

void EditorAIAgent::clear_session() {
	Dictionary params;
	send_jsonrpc("clearSession", params);
	session_usage = AITokenUsage();
	emit_signal("usage_updated", 0, 0);
}

void EditorAIAgent::add_context_item(AIContextItemKind p_kind, const String &p_path, const String &p_label) {
	Dictionary params;
	params["kind"] = ai_context_kind_to_string(p_kind);
	params["path"] = p_path;
	params["label"] = p_label.is_empty() ? p_path.get_file() : p_label;
	send_jsonrpc("context.add", params);
	refresh_context(); // Refresh to get updated list
}

void EditorAIAgent::remove_context_item(const String &p_item_id) {
	Dictionary params;
	params["itemId"] = p_item_id;
	send_jsonrpc("context.remove", params);
	refresh_context();
}

void EditorAIAgent::clear_context() {
	Dictionary params;
	send_jsonrpc("context.clear", params);
	refresh_context();
}

void EditorAIAgent::load_bundle(const String &p_name) {
	Dictionary params;
	params["name"] = p_name;
	send_jsonrpc("bundle.load", params);
	refresh_context();
}

void EditorAIAgent::save_bundle(const String &p_name) {
	Dictionary params;
	params["name"] = p_name;
	send_jsonrpc("bundle.save", params);
	refresh_bundles();
}

void EditorAIAgent::refresh_bundles() {
	Dictionary params;
	send_jsonrpc("bundle.list", params);
}

void EditorAIAgent::refresh_context() {
	Dictionary params;
	send_jsonrpc("context.get", params);
}

void EditorAIAgent::run_harness(const String &p_scene_path, int p_frames) {
	Dictionary params;
	params["scenePath"] = p_scene_path;
	params["frames"] = p_frames;
	send_jsonrpc("runHarness", params);
}
