/**************************************************************************/
/*  editor_ai_agent.cpp                                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GAMEABLE                                   */
/*                     AI-powered game development                        */
/**************************************************************************/

#include "editor_ai_agent.h"

#include "core/config/project_settings.h"
#include "core/core_bind.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/object/class_db.h"
#include "core/object/script_language.h"
#include "core/os/os.h"
#include "core/string/char_utils.h"
#include "core/string/print_string.h"
#include "editor/debugger/editor_debugger_node.h"
#include "editor/debugger/script_editor_debugger.h"
#include "editor/editor_interface.h"
#include "editor/editor_log.h"
#include "editor/editor_main_screen.h"
#include "editor/editor_node.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/run/editor_run_bar.h"
#include "editor/scene/3d/node_3d_editor_plugin.h"
#include "editor/script/script_editor_plugin.h"
#include "editor/settings/editor_settings.h"
#include "modules/websocket/websocket_peer.h"
#include "scene/main/node.h"
#include "scene/main/timer.h"
#include "scene/main/viewport.h"
#include "scene/resources/material.h"
#include "scene/resources/packed_scene.h"
#include "scene/resources/shader.h"
#include "servers/rendering/shader_language.h"
#include "servers/rendering/shader_preprocessor.h"
#include "servers/rendering/shader_types.h"
#include "servers/rendering/rendering_server.h"

EditorAIAgent *EditorAIAgent::singleton = nullptr;

static void _count_scene_node_types_for_capture(Node *p_node, Node *p_root, int &r_2d, int &r_3d) {
	if (!p_node || !p_root) {
		return;
	}
	if (p_node->is_class("Viewport") || (p_node != p_root && p_node->get_owner() != p_root)) {
		return;
	}

	if (p_node->is_class("CanvasItem")) {
		r_2d++;
	} else if (p_node->is_class("Node3D")) {
		r_3d++;
	}

	for (int i = 0; i < p_node->get_child_count(); i++) {
		_count_scene_node_types_for_capture(p_node->get_child(i), p_root, r_2d, r_3d);
	}
}

static Ref<Image> _capture_editor_viewport_image(const String &p_mode, String &r_label) {
	EditorNode *editor = EditorNode::get_singleton();
	if (!editor) {
		return Ref<Image>();
	}

	auto capture_2d = [&]() -> Ref<Image> {
		SubViewport *scene_root = editor->get_scene_root();
		if (!scene_root) {
			return Ref<Image>();
		}
		Ref<ViewportTexture> texture = scene_root->get_texture();
		if (!texture.is_valid() || texture->get_width() <= 0 || texture->get_height() <= 0) {
			return Ref<Image>();
		}
		return texture->get_image();
	};

	auto capture_3d = [&]() -> Ref<Image> {
		Node3DEditor *editor_3d = Node3DEditor::get_singleton();
		if (!editor_3d) {
			return Ref<Image>();
		}
		Node3DEditorViewport *viewport = editor_3d->get_editor_viewport(0);
		if (!viewport) {
			return Ref<Image>();
		}
		Viewport *vp_node = viewport->get_viewport_node();
		if (!vp_node) {
			return Ref<Image>();
		}
		Ref<ViewportTexture> texture = vp_node->get_texture();
		if (!texture.is_valid() || texture->get_width() <= 0 || texture->get_height() <= 0) {
			return Ref<Image>();
		}
		return texture->get_image();
	};

	String mode = p_mode.to_lower();
	Ref<Image> image;
	if (mode == "2d") {
		image = capture_2d();
		if (image.is_valid()) {
			r_label = "Viewport 2D";
		}
	} else if (mode == "3d") {
		image = capture_3d();
		if (image.is_valid()) {
			r_label = "Viewport 3D";
		}
	} else {
		int selected = -1;
		if (EditorMainScreen *main_screen = editor->get_editor_main_screen()) {
			selected = main_screen->get_selected_index();
		}
		if (selected == EditorMainScreen::EDITOR_2D) {
			image = capture_2d();
			if (image.is_valid()) {
				r_label = "Viewport 2D";
			}
		} else if (selected == EditorMainScreen::EDITOR_3D) {
			image = capture_3d();
			if (image.is_valid()) {
				r_label = "Viewport 3D";
			}
		}
	}

	if (!image.is_valid()) {
		int c2d = 0;
		int c3d = 0;
		if (Node *root = editor->get_edited_scene()) {
			_count_scene_node_types_for_capture(root, root, c2d, c3d);
		}
		if (c3d >= c2d) {
			image = capture_3d();
			if (image.is_valid()) {
				r_label = "Viewport 3D";
			}
		}
		if (!image.is_valid()) {
			image = capture_2d();
			if (image.is_valid()) {
				r_label = "Viewport 2D";
			}
		}
	}

	if (image.is_valid()) {
		return image->duplicate();
	}

	return image;
}

void EditorAIAgent::_bind_methods() {
	// Signals for UI binding
	ADD_SIGNAL(MethodInfo("connection_state_changed", PropertyInfo(Variant::INT, "state")));
	ADD_SIGNAL(MethodInfo("processing_state_changed", PropertyInfo(Variant::BOOL, "is_processing")));
	ADD_SIGNAL(MethodInfo("thinking", PropertyInfo(Variant::STRING, "text")));
	ADD_SIGNAL(MethodInfo("status", PropertyInfo(Variant::STRING, "level"), PropertyInfo(Variant::STRING, "message")));
	ADD_SIGNAL(MethodInfo("usage_updated", PropertyInfo(Variant::INT, "turn_tokens"), PropertyInfo(Variant::INT, "session_tokens"), PropertyInfo(Variant::INT, "cache_rate")));
	ADD_SIGNAL(MethodInfo("tool_call", PropertyInfo(Variant::STRING, "id"), PropertyInfo(Variant::STRING, "name"), PropertyInfo(Variant::DICTIONARY, "input")));
	ADD_SIGNAL(MethodInfo("tool_result", PropertyInfo(Variant::STRING, "id"), PropertyInfo(Variant::BOOL, "ok"), PropertyInfo(Variant::DICTIONARY, "output")));
	ADD_SIGNAL(MethodInfo("tool_progress", PropertyInfo(Variant::STRING, "id"), PropertyInfo(Variant::STRING, "stage"), PropertyInfo(Variant::FLOAT, "progress")));
	ADD_SIGNAL(MethodInfo("verify_result", PropertyInfo(Variant::DICTIONARY, "result")));
	ADD_SIGNAL(MethodInfo("context_updated", PropertyInfo(Variant::ARRAY, "items")));
	ADD_SIGNAL(MethodInfo("bundles_updated", PropertyInfo(Variant::PACKED_STRING_ARRAY, "names")));
	ADD_SIGNAL(MethodInfo("chat_message", PropertyInfo(Variant::STRING, "role"), PropertyInfo(Variant::STRING, "text")));
	ADD_SIGNAL(MethodInfo("runtime_state_changed", PropertyInfo(Variant::STRING, "state"), PropertyInfo(Variant::STRING, "scene_path")));
	ADD_SIGNAL(MethodInfo("runtime_log_added", PropertyInfo(Variant::STRING, "text"), PropertyInfo(Variant::STRING, "level")));
	ADD_SIGNAL(MethodInfo("runtime_summary", PropertyInfo(Variant::DICTIONARY, "summary")));
	ADD_SIGNAL(MethodInfo("runtime_chat_message", PropertyInfo(Variant::STRING, "role"), PropertyInfo(Variant::STRING, "text")));
	ADD_SIGNAL(MethodInfo("runtime_thinking", PropertyInfo(Variant::STRING, "text")));
	ADD_SIGNAL(MethodInfo("runtime_tool_call", PropertyInfo(Variant::STRING, "id"), PropertyInfo(Variant::STRING, "name"), PropertyInfo(Variant::DICTIONARY, "input")));
	ADD_SIGNAL(MethodInfo("runtime_tool_result", PropertyInfo(Variant::STRING, "id"), PropertyInfo(Variant::BOOL, "ok"), PropertyInfo(Variant::DICTIONARY, "output")));
	ADD_SIGNAL(MethodInfo("runtime_tool_progress", PropertyInfo(Variant::STRING, "id"), PropertyInfo(Variant::STRING, "stage"), PropertyInfo(Variant::FLOAT, "progress")));
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
	runtime_chat_active = false;
	connection_state = AI_CONNECTION_DISCONNECTED;
	emit_signal("connection_state_changed", (int)connection_state);
}

bool EditorAIAgent::is_agent_connected() const {
	return ws && ws->get_ready_state() == WebSocketPeer::STATE_OPEN;
}

void EditorAIAgent::_ensure_runtime_hooks() {
	if (!EditorNode::get_singleton()) {
		return;
	}

	if (!runbar_connected) {
		if (EditorRunBar *run_bar = EditorRunBar::get_singleton()) {
			run_bar->connect("play_pressed", callable_mp(this, &EditorAIAgent::_on_play_pressed));
			run_bar->connect("stop_pressed", callable_mp(this, &EditorAIAgent::_on_stop_pressed));
			runbar_connected = true;
		}
	}

	if (!log_connected) {
		if (EditorLog *log = EditorNode::get_singleton()->get_log()) {
			log->connect("message_added", callable_mp(this, &EditorAIAgent::_on_editor_log_message));
			log_connected = true;
		}
	}

	if (!debugger_connected) {
		if (EditorDebuggerNode *debugger_node = EditorDebuggerNode::get_singleton()) {
			if (ScriptEditorDebugger *debugger = debugger_node->get_default_debugger()) {
				debugger->connect("error_logged", callable_mp(this, &EditorAIAgent::_on_debugger_error_logged));
				debugger_connected = true;
			}
		}
	}
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

	// 8MB outbound buffer for large payloads like screenshots
	ws->set_outbound_buffer_size((1 << 23) - 1);

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
	_ensure_runtime_hooks();
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

	// JSON-RPC request (has method and id)
	if (d.has("method") && d.has("id")) {
		String method = d["method"];
		Dictionary params = d.has("params") ? Dictionary(d["params"]) : Dictionary();
		_handle_request(method, params, int(d["id"]));
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

static ScriptLanguage *_find_language_for_extension(const String &p_ext) {
	for (int i = 0; i < ScriptServer::get_language_count(); i++) {
		ScriptLanguage *lang = ScriptServer::get_language(i);
		if (lang && lang->get_extension() == p_ext) {
			return lang;
		}
	}
	return nullptr;
}

struct DiagnosticPrintCapture {
	PrintHandlerList handler;
	Vector<String> errors;
	Vector<String> warnings;
	int max_entries = 200;

	static void _handle_print(void *p_userdata, const String &p_string, bool p_error, bool p_rich) {
		(void)p_rich;
		DiagnosticPrintCapture *self = static_cast<DiagnosticPrintCapture *>(p_userdata);
		if (!self) {
			return;
		}
		if (p_error) {
			if (self->errors.size() < self->max_entries) {
				self->errors.push_back(p_string);
			}
			return;
		}
		const String trimmed = p_string.strip_edges();
		if (trimmed.begins_with("WARNING:")) {
			if (self->warnings.size() < self->max_entries) {
				self->warnings.push_back(p_string);
			}
		}
	}

	DiagnosticPrintCapture() {
		handler.printfunc = _handle_print;
		handler.userdata = this;
		add_print_handler(&handler);
	}

	~DiagnosticPrintCapture() {
		remove_print_handler(&handler);
	}
};

static Node *_find_node_for_path(Node *p_root, const String &p_node_path) {
	if (!p_root) {
		return nullptr;
	}
	if (p_node_path.is_empty() || p_node_path == "." || p_node_path == "/") {
		return p_root;
	}
	return p_root->get_node_or_null(NodePath(p_node_path));
}

static Node *_get_scene_root_for_edit(const String &p_scene_path, Ref<PackedScene> &r_scene, bool &r_using_open, String &r_error) {
	r_using_open = false;
	r_scene = Ref<PackedScene>();
	if (EditorNode::get_singleton()) {
		Node *edited = EditorNode::get_singleton()->get_edited_scene();
		if (edited && edited->get_scene_file_path() == p_scene_path) {
			r_using_open = true;
			return edited;
		}
	}

	Error err = OK;
	Ref<Resource> res = ResourceLoader::load(p_scene_path, "", ResourceFormatLoader::CACHE_MODE_REUSE, &err);
	if (err != OK || res.is_null()) {
		r_error = "Failed to load scene";
		return nullptr;
	}
	r_scene = res;
	if (r_scene.is_null()) {
		r_error = "Scene is not a PackedScene";
		return nullptr;
	}
	Node *root = r_scene->instantiate();
	if (!root) {
		r_error = "Failed to instantiate scene";
		return nullptr;
	}
	return root;
}

static bool _save_scene_from_root(const String &p_scene_path, Ref<PackedScene> &p_scene, Node *p_root, bool p_using_open, String &r_error) {
	if (p_using_open) {
		if (!EditorNode::get_singleton()) {
			r_error = "EditorNode unavailable";
			return false;
		}
		EditorNode::get_singleton()->save_scene_to_path(p_scene_path, false);
		return true;
	}
	if (p_scene.is_null()) {
		r_error = "PackedScene missing";
		return false;
	}
	Error pack_err = p_scene->pack(p_root);
	if (pack_err != OK) {
		r_error = "Failed to pack scene";
		return false;
	}
	Error save_err = ResourceSaver::save(p_scene, p_scene_path, ResourceSaver::FLAG_CHANGE_PATH);
	if (save_err != OK) {
		r_error = "Failed to save scene";
		return false;
	}
	memdelete(p_root);
	return true;
}

static String _to_res_path(const String &p_path) {
	if (p_path.begins_with("res://")) {
		return p_path;
	}
	if (ProjectSettings::get_singleton()) {
		const String root = ProjectSettings::get_singleton()->get_resource_path();
		if (p_path.begins_with(root)) {
			const String rel = p_path.substr(root.length() + 1);
			return "res://" + rel;
		}
	}
	return p_path;
}

static bool _variant_is_number(const Variant &p_value) {
	return p_value.get_type() == Variant::INT || p_value.get_type() == Variant::FLOAT;
}

static double _variant_to_double(const Variant &p_value) {
	if (p_value.get_type() == Variant::INT) {
		return (int64_t)p_value;
	}
	if (p_value.get_type() == Variant::FLOAT) {
		return (double)p_value;
	}
	return 0.0;
}

static bool _try_parse_nodepath_literal(const String &p_value, NodePath &r_path) {
	String trimmed = p_value.strip_edges();
	if (!trimmed.begins_with("NodePath(") || !trimmed.ends_with(")")) {
		return false;
	}
	String inner = trimmed.substr(9, trimmed.length() - 10).strip_edges();
	if ((inner.begins_with("\"") && inner.ends_with("\"")) || (inner.begins_with("'") && inner.ends_with("'"))) {
		inner = inner.substr(1, inner.length() - 2);
	}
	r_path = NodePath(inner);
	return true;
}

static Variant _coerce_resource_value(const Variant &p_value) {
	if (p_value.get_type() == Variant::STRING) {
		String raw = p_value;
		NodePath node_path;
		if (_try_parse_nodepath_literal(raw, node_path)) {
			return node_path;
		}
		return raw;
	}
	if (p_value.get_type() == Variant::ARRAY) {
		Array arr = p_value;
		const int size = arr.size();
		bool all_numbers = size > 0;
		for (int i = 0; i < size; i++) {
			if (!_variant_is_number(arr[i])) {
				all_numbers = false;
				break;
			}
		}
		if (all_numbers) {
			if (size == 2) {
				return Vector2((real_t)_variant_to_double(arr[0]), (real_t)_variant_to_double(arr[1]));
			}
			if (size == 3) {
				return Vector3((real_t)_variant_to_double(arr[0]), (real_t)_variant_to_double(arr[1]), (real_t)_variant_to_double(arr[2]));
			}
			if (size == 4) {
				return Color((real_t)_variant_to_double(arr[0]), (real_t)_variant_to_double(arr[1]), (real_t)_variant_to_double(arr[2]), (real_t)_variant_to_double(arr[3]));
			}
		}
		Array out;
		out.resize(size);
		for (int i = 0; i < size; i++) {
			out[i] = _coerce_resource_value(arr[i]);
		}
		return out;
	}
	if (p_value.get_type() == Variant::DICTIONARY) {
		Dictionary dict = p_value;
		Dictionary out;
		Array keys = dict.keys();
		for (int i = 0; i < keys.size(); i++) {
			const Variant key = keys[i];
			out[key] = _coerce_resource_value(dict[key]);
		}
		return out;
	}
	return p_value;
}

static String _shader_mode_to_string(Shader::Mode p_mode) {
	switch (p_mode) {
		case Shader::MODE_SPATIAL:
			return "spatial";
		case Shader::MODE_CANVAS_ITEM:
			return "canvas_item";
		case Shader::MODE_PARTICLES:
			return "particles";
		case Shader::MODE_SKY:
			return "sky";
		case Shader::MODE_FOG:
			return "fog";
		default:
			return "unknown";
	}
}

static Variant _variant_to_json(const Variant &p_value) {
	switch (p_value.get_type()) {
		case Variant::NIL:
			return Variant();
		case Variant::BOOL:
		case Variant::INT:
		case Variant::FLOAT:
		case Variant::STRING:
			return p_value;
		case Variant::STRING_NAME:
			return String(p_value);
		case Variant::NODE_PATH: {
			NodePath path = p_value;
			return String("NodePath(\"") + String(path).c_escape() + "\")";
		}
		case Variant::VECTOR2: {
			Vector2 v = p_value;
			Array out;
			out.push_back(v.x);
			out.push_back(v.y);
			return out;
		}
		case Variant::VECTOR2I: {
			Vector2i v = p_value;
			Array out;
			out.push_back(v.x);
			out.push_back(v.y);
			return out;
		}
		case Variant::VECTOR3: {
			Vector3 v = p_value;
			Array out;
			out.push_back(v.x);
			out.push_back(v.y);
			out.push_back(v.z);
			return out;
		}
		case Variant::VECTOR3I: {
			Vector3i v = p_value;
			Array out;
			out.push_back(v.x);
			out.push_back(v.y);
			out.push_back(v.z);
			return out;
		}
		case Variant::VECTOR4: {
			Vector4 v = p_value;
			Array out;
			out.push_back(v.x);
			out.push_back(v.y);
			out.push_back(v.z);
			out.push_back(v.w);
			return out;
		}
		case Variant::VECTOR4I: {
			Vector4i v = p_value;
			Array out;
			out.push_back(v.x);
			out.push_back(v.y);
			out.push_back(v.z);
			out.push_back(v.w);
			return out;
		}
		case Variant::RECT2: {
			Rect2 r = p_value;
			Array out;
			out.push_back(r.position.x);
			out.push_back(r.position.y);
			out.push_back(r.size.x);
			out.push_back(r.size.y);
			return out;
		}
		case Variant::RECT2I: {
			Rect2i r = p_value;
			Array out;
			out.push_back(r.position.x);
			out.push_back(r.position.y);
			out.push_back(r.size.x);
			out.push_back(r.size.y);
			return out;
		}
		case Variant::PLANE: {
			Plane p = p_value;
			Array out;
			out.push_back(p.normal.x);
			out.push_back(p.normal.y);
			out.push_back(p.normal.z);
			out.push_back(p.d);
			return out;
		}
		case Variant::QUATERNION: {
			Quaternion q = p_value;
			Array out;
			out.push_back(q.x);
			out.push_back(q.y);
			out.push_back(q.z);
			out.push_back(q.w);
			return out;
		}
		case Variant::COLOR: {
			Color c = p_value;
			Array out;
			out.push_back(c.r);
			out.push_back(c.g);
			out.push_back(c.b);
			out.push_back(c.a);
			return out;
		}
		case Variant::ARRAY: {
			Array arr = p_value;
			Array out;
			out.resize(arr.size());
			for (int i = 0; i < arr.size(); i++) {
				out[i] = _variant_to_json(arr[i]);
			}
			return out;
		}
		case Variant::DICTIONARY: {
			Dictionary dict = p_value;
			Dictionary out;
			Array keys = dict.keys();
			for (int i = 0; i < keys.size(); i++) {
				Variant key = keys[i];
				out[key] = _variant_to_json(dict[key]);
			}
			return out;
		}
		case Variant::OBJECT: {
			Object *obj = p_value;
			if (!obj) {
				return Variant();
			}
			if (Resource *res = Object::cast_to<Resource>(obj)) {
				String res_path = res->get_path();
				if (!res_path.is_empty()) {
					return _to_res_path(res_path);
				}
				return String(res->get_class());
			}
			return String(obj->get_class());
		}
		case Variant::PACKED_INT32_ARRAY: {
			PackedInt32Array arr = p_value;
			Array out;
			out.resize(arr.size());
			for (int i = 0; i < arr.size(); i++) {
				out[i] = arr[i];
			}
			return out;
		}
		case Variant::PACKED_INT64_ARRAY: {
			PackedInt64Array arr = p_value;
			Array out;
			out.resize(arr.size());
			for (int i = 0; i < arr.size(); i++) {
				out[i] = arr[i];
			}
			return out;
		}
		case Variant::PACKED_FLOAT32_ARRAY: {
			PackedFloat32Array arr = p_value;
			Array out;
			out.resize(arr.size());
			for (int i = 0; i < arr.size(); i++) {
				out[i] = arr[i];
			}
			return out;
		}
		case Variant::PACKED_FLOAT64_ARRAY: {
			PackedFloat64Array arr = p_value;
			Array out;
			out.resize(arr.size());
			for (int i = 0; i < arr.size(); i++) {
				out[i] = arr[i];
			}
			return out;
		}
		case Variant::PACKED_STRING_ARRAY: {
			PackedStringArray arr = p_value;
			Array out;
			out.resize(arr.size());
			for (int i = 0; i < arr.size(); i++) {
				out[i] = arr[i];
			}
			return out;
		}
		case Variant::PACKED_VECTOR2_ARRAY: {
			PackedVector2Array arr = p_value;
			Array out;
			out.resize(arr.size());
			for (int i = 0; i < arr.size(); i++) {
				Vector2 v = arr[i];
				Array row;
				row.push_back(v.x);
				row.push_back(v.y);
				out[i] = row;
			}
			return out;
		}
		case Variant::PACKED_VECTOR3_ARRAY: {
			PackedVector3Array arr = p_value;
			Array out;
			out.resize(arr.size());
			for (int i = 0; i < arr.size(); i++) {
				Vector3 v = arr[i];
				Array row;
				row.push_back(v.x);
				row.push_back(v.y);
				row.push_back(v.z);
				out[i] = row;
			}
			return out;
		}
		case Variant::PACKED_COLOR_ARRAY: {
			PackedColorArray arr = p_value;
			Array out;
			out.resize(arr.size());
			for (int i = 0; i < arr.size(); i++) {
				Color c = arr[i];
				Array row;
				row.push_back(c.r);
				row.push_back(c.g);
				row.push_back(c.b);
				row.push_back(c.a);
				out[i] = row;
			}
			return out;
		}
		default:
			return String(p_value);
	}
}

static bool _property_is_group(const PropertyInfo &p_info) {
	return (p_info.usage & PROPERTY_USAGE_GROUP) || (p_info.usage & PROPERTY_USAGE_SUBGROUP) || (p_info.usage & PROPERTY_USAGE_CATEGORY);
}

static Dictionary _property_info_to_dict(const PropertyInfo &p_info, const Variant &p_value, bool p_include_value) {
	Dictionary d;
	d["name"] = p_info.name;
	d["type"] = Variant::get_type_name(p_info.type);
	d["typeId"] = int(p_info.type);
	d["hint"] = int(p_info.hint);
	d["hintString"] = p_info.hint_string;
	d["usage"] = int(p_info.usage);
	if (p_info.class_name != StringName()) {
		d["className"] = String(p_info.class_name);
	}
	if (p_include_value) {
		d["value"] = _variant_to_json(p_value);
	}
	return d;
}

static Array _build_property_list(Object *p_target, bool p_include_values) {
	Array out;
	if (!p_target) {
		return out;
	}
	List<PropertyInfo> props;
	p_target->get_property_list(&props);
	for (const PropertyInfo &pi : props) {
		bool include_value = p_include_values && !_property_is_group(pi);
		Variant value;
		if (include_value) {
			bool valid = true;
			value = p_target->get(pi.name, &valid);
			if (!valid) {
				include_value = false;
			}
		}
		out.push_back(_property_info_to_dict(pi, value, include_value));
	}
	return out;
}

static Array _build_shader_uniforms(const Ref<Shader> &p_shader, ShaderMaterial *p_material, bool p_include_values) {
	Array out;
	if (!p_shader.is_valid()) {
		return out;
	}
	List<PropertyInfo> uniforms;
	p_shader->get_shader_uniform_list(&uniforms);
	for (const PropertyInfo &pi : uniforms) {
		bool include_value = p_include_values && p_material != nullptr && !_property_is_group(pi);
		Variant value;
		if (include_value) {
			value = p_material->get_shader_parameter(pi.name);
		}
		out.push_back(_property_info_to_dict(pi, value, include_value));
	}
	return out;
}

static Array _build_shader_default_textures(const Ref<Shader> &p_shader) {
	Array out;
	if (!p_shader.is_valid()) {
		return out;
	}
	List<StringName> textures;
	p_shader->get_default_texture_parameter_list(&textures);
	for (const StringName &name : textures) {
		Dictionary entry;
		entry["name"] = String(name);
		Ref<Texture> tex = p_shader->get_default_texture_parameter(name, 0);
		if (tex.is_valid()) {
			String path = tex->get_path();
			if (!path.is_empty()) {
				entry["path"] = _to_res_path(path);
			}
		}
		out.push_back(entry);
	}
	return out;
}

static bool _set_nested_variant(Variant &p_target, const PackedStringArray &p_segments, int p_index, const Variant &p_value, String &r_error) {
	if (p_index >= p_segments.size()) {
		p_target = p_value;
		return true;
	}

	const String segment = p_segments[p_index];
	if (segment.is_valid_int()) {
		if (p_target.get_type() != Variant::ARRAY) {
			r_error = "Expected Array for indexed access";
			return false;
		}
		Array arr = p_target;
		const int index = segment.to_int();
		if (index < 0 || index >= arr.size()) {
			r_error = "Index out of range";
			return false;
		}
		Variant child = arr[index];
		if (!_set_nested_variant(child, p_segments, p_index + 1, p_value, r_error)) {
			return false;
		}
		arr[index] = child;
		p_target = arr;
		return true;
	}

	if (p_target.get_type() == Variant::DICTIONARY) {
		Dictionary dict = p_target;
		if (!dict.has(segment)) {
			r_error = "Dictionary key not found";
			return false;
		}
		Variant child = dict[segment];
		if (!_set_nested_variant(child, p_segments, p_index + 1, p_value, r_error)) {
			return false;
		}
		dict[segment] = child;
		p_target = dict;
		return true;
	}

	if (p_target.get_type() == Variant::OBJECT) {
		Object *obj = p_target;
		if (!obj) {
			r_error = "Invalid object in property path";
			return false;
		}
		if (p_index == p_segments.size() - 1) {
			bool valid = true;
			obj->set(segment, p_value, &valid);
			if (!valid) {
				r_error = "Invalid property path";
				return false;
			}
			return true;
		}
		bool valid = true;
		Variant child = obj->get(segment, &valid);
		if (!valid) {
			r_error = "Invalid property path";
			return false;
		}
		if (!_set_nested_variant(child, p_segments, p_index + 1, p_value, r_error)) {
			return false;
		}
		obj->set(segment, child, &valid);
		if (!valid) {
			r_error = "Invalid property path";
			return false;
		}
		return true;
	}

	r_error = "Invalid property path";
	return false;
}

static bool _set_resource_property_path(Object *p_target, const String &p_property, const Variant &p_value, String &r_error) {
	if (!p_target) {
		r_error = "Invalid resource";
		return false;
	}

	bool valid = true;
	p_target->set(StringName(p_property), p_value, &valid);
	if (valid) {
		return true;
	}

	if (p_property.find("/") < 0) {
		r_error = "Invalid property path";
		return false;
	}

	PackedStringArray segments = p_property.split("/", false);
	if (segments.size() < 2) {
		r_error = "Invalid property path";
		return false;
	}

	Variant root_value = p_target->get(segments[0], &valid);
	if (!valid) {
		r_error = "Invalid property path";
		return false;
	}
	if (!_set_nested_variant(root_value, segments, 1, p_value, r_error)) {
		return false;
	}
	p_target->set(StringName(segments[0]), root_value, &valid);
	if (!valid) {
		r_error = "Invalid property path";
		return false;
	}
	return true;
}
static String _find_csproj_path(const String &p_project_root) {
	Ref<DirAccess> dir = DirAccess::open(p_project_root);
	if (dir.is_null()) {
		return String();
	}
	dir->list_dir_begin();
	while (true) {
		String file = dir->get_next();
		if (file.is_empty()) {
			break;
		}
		if (dir->current_is_dir()) {
			continue;
		}
		if (file.get_extension().to_lower() == "csproj") {
			dir->list_dir_end();
			return p_project_root.path_join(file);
		}
	}
	dir->list_dir_end();
	return String();
}

static ShaderLanguage::DataType _get_global_shader_uniform_type(const StringName &p_variable) {
	RenderingServer::GlobalShaderParameterType gvt = RenderingServer::get_singleton()->global_shader_parameter_get_type(p_variable);
	return (ShaderLanguage::DataType)RenderingServer::global_shader_uniform_type_get_shader_datatype(gvt);
}

static RenderingServer::ShaderMode _shader_mode_from_type(const String &p_type) {
	if (p_type == "canvas_item") {
		return RenderingServer::SHADER_CANVAS_ITEM;
	}
	if (p_type == "particles") {
		return RenderingServer::SHADER_PARTICLES;
	}
	if (p_type == "sky") {
		return RenderingServer::SHADER_SKY;
	}
	if (p_type == "fog") {
		return RenderingServer::SHADER_FOG;
	}
	return RenderingServer::SHADER_SPATIAL;
}

static void _add_diagnostic(Array &r_diags, const String &p_path, int p_line, int p_column, const String &p_severity, const String &p_message, const String &p_source) {
	Dictionary d;
	d["path"] = p_path;
	d["line"] = p_line;
	d["column"] = p_column;
	d["severity"] = p_severity;
	d["message"] = p_message;
	d["source"] = p_source;
	r_diags.push_back(d);
}

static String _diagnostic_source_from_path(const String &p_path) {
	const String ext = p_path.get_extension().to_lower();
	if (ext == "gd") {
		return "gdscript";
	}
	if (ext == "tscn") {
		return "scene";
	}
	if (ext == "gdshader" || ext == "gdshaderinc") {
		return "shader";
	}
	if (ext == "cs") {
		return "csharp";
	}
	return "engine";
}

static String _strip_log_prefix(const String &p_line) {
	String line = p_line.strip_edges();
	if (line.begins_with("ERROR:")) {
		line = line.substr(6).strip_edges();
	} else if (line.begins_with("WARNING:")) {
		line = line.substr(8).strip_edges();
	}
	return line;
}

static bool _parse_print_log_line(const String &p_line, String &r_path, int &r_line, int &r_column, String &r_message) {
	String line = _strip_log_prefix(p_line);
	int res_index = line.find("res://");
	if (res_index < 0) {
		r_message = line;
		return false;
	}

	// Handle Godot's "[Resource file res://path:line]" format where message is BEFORE the bracket.
	// e.g.: "Parse Error: Parse error. [Resource file res://node_3d.tscn:85]"
	int bracket_start = line.rfind("[", res_index);
	String prefix_msg;
	if (bracket_start > 0) {
		prefix_msg = line.substr(0, bracket_start).strip_edges();
	}

	int colon = -1;
	for (int i = line.find(":", res_index + 1); i >= 0; i = line.find(":", i + 1)) {
		int j = i + 1;
		while (j < line.length() && line[j] == ' ') {
			j++;
		}
		if (j < line.length() && is_digit(line[j])) {
			colon = i;
			break;
		}
	}
	if (colon < 0) {
		r_message = line;
		return false;
	}

	r_path = line.substr(res_index, colon - res_index);
	int pos = colon + 1;
	while (pos < line.length() && line[pos] == ' ') {
		pos++;
	}
	const int line_start = pos;
	while (pos < line.length() && is_digit(line[pos])) {
		pos++;
	}
	if (pos == line_start) {
		r_message = line;
		return false;
	}
	r_line = line.substr(line_start, pos - line_start).to_int();
	r_column = 1;

	if (pos < line.length() && line[pos] == ':') {
		int col_start = pos + 1;
		while (col_start < line.length() && line[col_start] == ' ') {
			col_start++;
		}
		int col_end = col_start;
		while (col_end < line.length() && is_digit(line[col_end])) {
			col_end++;
		}
		if (col_end > col_start) {
			r_column = line.substr(col_start, col_end - col_start).to_int();
			pos = col_end;
		}
	}

	// Prefer prefix message (before [Resource file...]) if available, as Godot puts
	// the actual error there. Otherwise fall back to suffix extraction.
	if (!prefix_msg.is_empty()) {
		r_message = prefix_msg;
		return true;
	}

	int msg_index = line.find(" - ", pos);
	if (msg_index >= 0) {
		r_message = line.substr(msg_index + 3).strip_edges();
	} else if (pos < line.length()) {
		r_message = line.substr(pos).strip_edges();
	} else {
		r_message = line;
	}
	return true;
}

static void _append_print_diagnostics(Array &r_diags, const String &p_fallback_path, const Vector<String> &p_errors, const Vector<String> &p_warnings) {
	const String fallback = _to_res_path(p_fallback_path);
	for (int i = 0; i < p_errors.size(); i++) {
		String path;
		int line = 1;
		int column = 1;
		String message;
		bool parsed = _parse_print_log_line(p_errors[i], path, line, column, message);
		const String diag_path = parsed ? _to_res_path(path) : fallback;
		if (diag_path.is_empty()) {
			continue;
		}
		const String source = _diagnostic_source_from_path(diag_path);
		if (message.is_empty()) {
			message = _strip_log_prefix(p_errors[i]);
		}
		_add_diagnostic(r_diags, diag_path, line, column, "error", message, source);
	}
	for (int i = 0; i < p_warnings.size(); i++) {
		String path;
		int line = 1;
		int column = 1;
		String message;
		bool parsed = _parse_print_log_line(p_warnings[i], path, line, column, message);
		const String diag_path = parsed ? _to_res_path(path) : fallback;
		if (diag_path.is_empty()) {
			continue;
		}
		const String source = _diagnostic_source_from_path(diag_path);
		if (message.is_empty()) {
			message = _strip_log_prefix(p_warnings[i]);
		}
		_add_diagnostic(r_diags, diag_path, line, column, "warning", message, source);
	}
}

// Collect script paths from a node tree (for validating scripts attached to scene nodes)
static void _collect_node_script_paths(Node *p_root, HashSet<String> &r_scripts) {
	if (!p_root) {
		return;
	}
	Vector<Node *> stack;
	stack.push_back(p_root);
	while (!stack.is_empty()) {
		Node *node = stack[stack.size() - 1];
		stack.remove_at(stack.size() - 1);
		if (!node) {
			continue;
		}
		Ref<Script> script = node->get_script();
		if (script.is_valid()) {
			String script_path = script->get_path();
			if (!script_path.is_empty() && script_path.begins_with("res://")) {
				r_scripts.insert(script_path);
			}
		}
		const int child_count = node->get_child_count();
		for (int i = 0; i < child_count; i++) {
			stack.push_back(node->get_child(i));
		}
	}
}

// Validate a GDScript file and add diagnostics
static void _validate_gdscript_file(const String &p_res_path, Array &r_diags) {
	const String ext = p_res_path.get_extension().to_lower();
	if (ext != "gd") {
		return;
	}
	String text = FileAccess::get_file_as_string(p_res_path);
	ScriptLanguage *lang = _find_language_for_extension(ext);
	if (!lang) {
		return;
	}
	List<ScriptLanguage::ScriptError> errors;
	List<ScriptLanguage::Warning> warnings;
	lang->validate(text, p_res_path, nullptr, &errors, &warnings, nullptr);
	for (const ScriptLanguage::ScriptError &e : errors) {
		const String err_path = e.path.is_empty() ? p_res_path : _to_res_path(e.path);
		_add_diagnostic(r_diags, err_path, e.line, e.column, "error", e.message, "gdscript");
	}
	for (const ScriptLanguage::Warning &w : warnings) {
		const int line = w.start_line > 0 ? w.start_line : 1;
		_add_diagnostic(r_diags, p_res_path, line, 1, "warning", w.message, "gdscript");
	}
}

static void _collect_scene_configuration_warnings(Node *p_root, const String &p_scene_path, Array &r_diags) {
	if (!p_root) {
		return;
	}
	Vector<Node *> stack;
	stack.push_back(p_root);
	while (!stack.is_empty()) {
		Node *node = stack[stack.size() - 1];
		stack.remove_at(stack.size() - 1);
		if (!node) {
			continue;
		}
		node->update_configuration_warnings();
		PackedStringArray warnings = node->get_configuration_warnings();
		for (int i = 0; i < warnings.size(); i++) {
			const String warning = warnings[i];
			if (warning.is_empty()) {
				continue;
			}
			const String message = String(node->get_path()) + ": " + warning;
			_add_diagnostic(r_diags, p_scene_path, 1, 1, "warning", message, "scene");
		}
		const int child_count = node->get_child_count();
		for (int i = 0; i < child_count; i++) {
			stack.push_back(node->get_child(i));
		}
	}
}

void EditorAIAgent::_handle_request(const String &p_method, const Dictionary &p_params, int p_id) {
	if (p_method == "captureViewport") {
		String mode = p_params.has("mode") ? String(p_params["mode"]) : String("auto");
		String label;
		Ref<Image> image = _capture_editor_viewport_image(mode, label);

		Dictionary result;
		if (!image.is_valid() || image->get_width() <= 0 || image->get_height() <= 0) {
			result["ok"] = false;
			_send_jsonrpc_response(p_id, result);
			return;
		}

		Vector<uint8_t> buffer = image->save_png_to_buffer();
		if (buffer.is_empty()) {
			result["ok"] = false;
			_send_jsonrpc_response(p_id, result);
			return;
		}

		CoreBind::Marshalls *marshalls = CoreBind::Marshalls::get_singleton();
		if (!marshalls) {
			result["ok"] = false;
			_send_jsonrpc_response(p_id, result);
			return;
		}

		result["ok"] = true;
		result["screenshot"] = marshalls->raw_to_base64(buffer);
		result["mime"] = "image/png";
		if (!label.is_empty()) {
			result["label"] = label;
		}
		result["capturedAt"] = int64_t(OS::get_singleton()->get_unix_time());
		_send_jsonrpc_response(p_id, result);
		return;
	}

	if (p_method == "getDiagnostics") {
		Array diagnostics;
		bool run_csharp_build = false;
		String project_root;
		if (ProjectSettings::get_singleton()) {
			project_root = ProjectSettings::get_singleton()->get_resource_path();
		}

		PackedStringArray paths;
		if (p_params.has("paths")) {
			Variant v = p_params["paths"];
			if (v.get_type() == Variant::PACKED_STRING_ARRAY) {
				paths = v;
			} else if (v.get_type() == Variant::ARRAY) {
				Array arr = v;
				for (int i = 0; i < arr.size(); i++) {
					if (arr[i].get_type() == Variant::STRING) {
						paths.push_back(arr[i]);
					}
				}
			}
		}

		for (int i = 0; i < paths.size(); i++) {
			const String path = paths[i];
			const String res_path = _to_res_path(path);
			const String ext = res_path.get_extension().to_lower();
			DiagnosticPrintCapture print_capture;

			if (ext == "gd") {
				String text = FileAccess::get_file_as_string(res_path);
				ScriptLanguage *lang = _find_language_for_extension(ext);
				if (!lang) {
					_append_print_diagnostics(diagnostics, res_path, print_capture.errors, print_capture.warnings);
					continue;
				}

				List<ScriptLanguage::ScriptError> errors;
				List<ScriptLanguage::Warning> warnings;
				lang->validate(text, path, nullptr, &errors, &warnings, nullptr);

				for (const ScriptLanguage::ScriptError &e : errors) {
					const String err_path = e.path.is_empty() ? path : e.path;
					_add_diagnostic(diagnostics, _to_res_path(err_path), e.line, e.column, "error", e.message, "gdscript");
				}
				for (const ScriptLanguage::Warning &w : warnings) {
					const int line = w.start_line > 0 ? w.start_line : 1;
					_add_diagnostic(diagnostics, res_path, line, 1, "warning", w.message, "gdscript");
				}
				_append_print_diagnostics(diagnostics, res_path, print_capture.errors, print_capture.warnings);
				continue;
			}

			if (ext == "tscn") {
				Error err = OK;
				Ref<Resource> res = ResourceLoader::load(res_path, "", ResourceFormatLoader::CACHE_MODE_REUSE, &err);
				if (err != OK || res.is_null()) {
					_add_diagnostic(diagnostics, res_path, 1, 1, "error", "Failed to load scene", "scene");
					_append_print_diagnostics(diagnostics, res_path, print_capture.errors, print_capture.warnings);
					continue;
				}
				Ref<PackedScene> scene = res;
				if (scene.is_valid()) {
					Node *inst = scene->instantiate();
					if (!inst) {
						_add_diagnostic(diagnostics, res_path, 1, 1, "error", "Failed to instantiate scene", "scene");
					} else {
						_collect_scene_configuration_warnings(inst, res_path, diagnostics);
						// Collect and validate scripts attached to scene nodes
						HashSet<String> script_paths;
						_collect_node_script_paths(inst, script_paths);
						for (const String &script_path : script_paths) {
							_validate_gdscript_file(script_path, diagnostics);
						}
						memdelete(inst);
					}
				}
				_append_print_diagnostics(diagnostics, res_path, print_capture.errors, print_capture.warnings);
				continue;
			}

			if (ext == "gdshader" || ext == "gdshaderinc") {
				String code = FileAccess::get_file_as_string(res_path);
				ShaderPreprocessor preprocessor;
				String code_pp;
				String error_pp;
				List<ShaderPreprocessor::FilePosition> err_positions;
				List<ShaderPreprocessor::Region> regions;
				Error pp_err = preprocessor.preprocess(code, res_path, code_pp, &error_pp, &err_positions, &regions);
				if (pp_err != OK) {
					String err_path = path;
					int err_line = 1;
					if (!err_positions.is_empty()) {
						err_path = err_positions.front()->get().file;
						err_line = err_positions.front()->get().line;
					}
					String msg = error_pp.is_empty() ? "Shader preprocessor error" : error_pp;
					_add_diagnostic(diagnostics, _to_res_path(err_path), err_line, 1, "error", msg, "shader");
					continue;
				}

				ShaderLanguage sl;
				ShaderLanguage::ShaderCompileInfo comp_info;
				comp_info.global_shader_uniform_type_func = _get_global_shader_uniform_type;

				if (ext == "gdshaderinc") {
					comp_info.is_include = true;
				} else {
					const String shader_type = ShaderLanguage::get_shader_type(code_pp);
					const RenderingServer::ShaderMode mode = _shader_mode_from_type(shader_type);
					comp_info.functions = ShaderTypes::get_singleton()->get_functions(mode);
					comp_info.render_modes = ShaderTypes::get_singleton()->get_modes(mode);
					comp_info.stencil_modes = ShaderTypes::get_singleton()->get_stencil_modes(mode);
					comp_info.shader_types = ShaderTypes::get_singleton()->get_types();
				}

				Error comp_err = sl.compile(code_pp, comp_info);
				if (comp_err != OK) {
					Vector<ShaderLanguage::FilePosition> include_positions = sl.get_include_positions();
					String err_text = sl.get_error_text();
					String err_path = path;
					int err_line = sl.get_error_line();

					if (include_positions.size() > 1) {
						err_line = include_positions[0].line;
						err_path = include_positions[include_positions.size() - 1].file;
					} else if (include_positions.size() == 1 && !include_positions[0].file.is_empty()) {
						err_path = include_positions[0].file;
						if (include_positions[0].line > 0) {
							err_line = include_positions[0].line;
						}
					}
					_add_diagnostic(diagnostics, _to_res_path(err_path), err_line, 1, "error", err_text, "shader");
				}
				_append_print_diagnostics(diagnostics, res_path, print_capture.errors, print_capture.warnings);
				continue;
			}

			if (ext == "cs" || ext == "csproj") {
				run_csharp_build = true;
				_append_print_diagnostics(diagnostics, res_path, print_capture.errors, print_capture.warnings);
				continue;
			}

			_append_print_diagnostics(diagnostics, res_path, print_capture.errors, print_capture.warnings);
		}

		if (run_csharp_build) {
			String csproj = _find_csproj_path(project_root);
			if (csproj.is_empty()) {
				_add_diagnostic(diagnostics, "res://", 1, 1, "error", "C# project file not found", "csharp");
			} else {
				List<String> args;
				args.push_back("build");
				args.push_back(csproj);
				args.push_back("-nologo");
				args.push_back("-v:q");

				String output;
				int exit_code = 0;
				Error exec_err = OS::get_singleton()->execute("dotnet", args, &output, &exit_code, true);
				if (exec_err != OK) {
					_add_diagnostic(diagnostics, _to_res_path(csproj), 1, 1, "error", "dotnet build failed to run", "csharp");
				} else {
					PackedStringArray lines = output.split("\n", false);
					for (int i = 0; i < lines.size(); i++) {
						String line = lines[i].strip_edges();
						if (line.is_empty()) {
							continue;
						}
						int paren = line.find("(");
						int colon = line.find("):", paren);
						if (paren <= 0 || colon <= paren) {
							continue;
						}
						String file = line.substr(0, paren).strip_edges();
						String coords = line.substr(paren + 1, colon - paren - 1);
						PackedStringArray parts = coords.split(",", false);
						if (parts.size() < 2) {
							continue;
						}
						int line_no = parts[0].to_int();
						int col_no = parts[1].to_int();
						String severity = line.find("error") >= 0 ? "error" : (line.find("warning") >= 0 ? "warning" : "error");
						String message = line.substr(colon + 2).strip_edges();
						_add_diagnostic(diagnostics, _to_res_path(file), line_no, col_no, severity, message, "csharp");
					}
					if (exit_code != 0 && diagnostics.is_empty()) {
						_add_diagnostic(diagnostics, _to_res_path(csproj), 1, 1, "error", "C# build failed", "csharp");
					}
				}
			}
		}

		Dictionary result;
		result["diagnostics"] = diagnostics;
		_send_jsonrpc_response(p_id, result);
		return;
	}

	if (p_method == "resource.inspect") {
		String res_path = p_params.has("path") ? String(p_params["path"]) : String();
		bool include_properties = true;
		bool include_values = true;
		bool include_shader_uniforms = true;
		bool include_shader_code = false;

		if (p_params.has("includeProperties")) {
			include_properties = bool(p_params["includeProperties"]);
		}
		if (p_params.has("includeValues")) {
			include_values = bool(p_params["includeValues"]);
		}
		if (p_params.has("includeShaderUniforms")) {
			include_shader_uniforms = bool(p_params["includeShaderUniforms"]);
		}
		if (p_params.has("includeShaderCode")) {
			include_shader_code = bool(p_params["includeShaderCode"]);
		}

		Dictionary result;
		if (res_path.is_empty()) {
			result["ok"] = false;
			result["error"] = "Missing path";
			_send_jsonrpc_response(p_id, result);
			return;
		}

		res_path = _to_res_path(res_path);

		Error load_err = OK;
		Ref<Resource> resource = ResourceLoader::load(res_path, "", ResourceFormatLoader::CACHE_MODE_REUSE, &load_err);
		if (load_err != OK || resource.is_null()) {
			result["ok"] = false;
			result["error"] = "Failed to load resource";
			_send_jsonrpc_response(p_id, result);
			return;
		}

		result["ok"] = true;
		result["path"] = res_path;
		result["class"] = resource->get_class();

		if (include_properties) {
			result["properties"] = _build_property_list(resource.ptr(), include_values);
		}

		if (include_shader_uniforms || include_shader_code) {
			Ref<Shader> shader;
			ShaderMaterial *shader_material = Object::cast_to<ShaderMaterial>(resource.ptr());
			if (shader_material) {
				shader = shader_material->get_shader();
			}
			if (!shader.is_valid()) {
				Ref<Shader> as_shader = resource;
				if (as_shader.is_valid()) {
					shader = as_shader;
				}
			}
			if (shader.is_valid()) {
				Dictionary shader_info;
				shader_info["class"] = shader->get_class();
				String shader_path = shader->get_path();
				if (!shader_path.is_empty()) {
					shader_info["path"] = _to_res_path(shader_path);
				}
				shader_info["mode"] = _shader_mode_to_string(shader->get_mode());

				if (include_shader_uniforms) {
					Array uniforms = _build_shader_uniforms(shader, shader_material, include_values);
					if (!uniforms.is_empty()) {
						shader_info["uniforms"] = uniforms;
					}
					Array defaults = _build_shader_default_textures(shader);
					if (!defaults.is_empty()) {
						shader_info["defaultTextures"] = defaults;
					}
				}

				if (include_shader_code && shader->is_text_shader()) {
					shader_info["code"] = shader->get_code();
				}

				result["shader"] = shader_info;
			}
		}

		_send_jsonrpc_response(p_id, result);
		return;
	}

	if (p_method == "resource.setProperties") {
		String res_path = p_params.has("path") ? String(p_params["path"]) : String();
		Array updates = p_params.has("updates") ? Array(p_params["updates"]) : Array();

		Dictionary result;
		if (res_path.is_empty() || updates.is_empty()) {
			result["ok"] = false;
			result["error"] = "Missing path/updates";
			_send_jsonrpc_response(p_id, result);
			return;
		}

		res_path = _to_res_path(res_path);

		Error load_err = OK;
		Ref<Resource> resource = ResourceLoader::load(res_path, "", ResourceFormatLoader::CACHE_MODE_REUSE, &load_err);
		if (load_err != OK || resource.is_null()) {
			result["ok"] = false;
			result["error"] = "Failed to load resource";
			_send_jsonrpc_response(p_id, result);
			return;
		}

		for (int i = 0; i < updates.size(); i++) {
			if (updates[i].get_type() != Variant::DICTIONARY) {
				result["ok"] = false;
				result["error"] = "Invalid updates payload";
				_send_jsonrpc_response(p_id, result);
				return;
			}
			Dictionary update = updates[i];
			if (!update.has("property") || !update.has("value")) {
				result["ok"] = false;
				result["error"] = "Missing property/value";
				_send_jsonrpc_response(p_id, result);
				return;
			}
			String property = update["property"];
			Variant value = _coerce_resource_value(update["value"]);
			String error;
			if (!_set_resource_property_path(resource.ptr(), property, value, error)) {
				result["ok"] = false;
				result["error"] = error.is_empty() ? String("Invalid property path: ") + property : error;
				_send_jsonrpc_response(p_id, result);
				return;
			}
		}

		Error save_err = ResourceSaver::save(resource, res_path, ResourceSaver::FLAG_CHANGE_PATH);
		if (save_err != OK) {
			result["ok"] = false;
			result["error"] = "Failed to save resource";
			result["errorCode"] = int(save_err);
			_send_jsonrpc_response(p_id, result);
			return;
		}

		result["ok"] = true;
		result["path"] = res_path;
		_send_jsonrpc_response(p_id, result);
		return;
	}

	if (p_method == "scene.create") {
		String scene_path = p_params.has("path") ? String(p_params["path"]) : String();
		String root_type = p_params.has("rootType") ? String(p_params["rootType"]) : String();
		String root_name = p_params.has("rootName") ? String(p_params["rootName"]) : String();

		Dictionary result;
		if (scene_path.is_empty() || root_type.is_empty() || root_name.is_empty()) {
			result["ok"] = false;
			result["error"] = "Missing path/rootType/rootName";
			_send_jsonrpc_response(p_id, result);
			return;
		}

		scene_path = _to_res_path(scene_path);
		if (FileAccess::exists(scene_path)) {
			result["ok"] = false;
			result["error"] = "Scene already exists";
			_send_jsonrpc_response(p_id, result);
			return;
		}

		StringName root_type_name = StringName(root_type);
		if (!ClassDB::can_instantiate(root_type_name) || !ClassDB::is_parent_class(root_type_name, StringName("Node"))) {
			result["ok"] = false;
			result["error"] = "Invalid rootType (not a Node)";
			_send_jsonrpc_response(p_id, result);
			return;
		}

		if (ProjectSettings::get_singleton()) {
			String global_path = ProjectSettings::get_singleton()->globalize_path(scene_path);
			String dir_path = global_path.get_base_dir();
			Error dir_err = DirAccess::make_dir_recursive_absolute(dir_path);
			if (dir_err != OK) {
				result["ok"] = false;
				result["error"] = "Failed to create scene directory";
				_send_jsonrpc_response(p_id, result);
				return;
			}
		}

		Object *obj = ClassDB::instantiate(root_type_name);
		Node *root = Object::cast_to<Node>(obj);
		if (!root) {
			if (obj) {
				memdelete(obj);
			}
			result["ok"] = false;
			result["error"] = "Failed to instantiate root node";
			_send_jsonrpc_response(p_id, result);
			return;
		}

		root->set_name(root_name);

		Ref<PackedScene> packed_scene;
		packed_scene.instantiate();

		String err_msg;
		if (!_save_scene_from_root(scene_path, packed_scene, root, false, err_msg)) {
			if (root) {
				memdelete(root);
			}
			result["ok"] = false;
			result["error"] = err_msg.is_empty() ? String("Failed to save scene") : err_msg;
			_send_jsonrpc_response(p_id, result);
			return;
		}

		result["ok"] = true;
		result["path"] = scene_path;
		_send_jsonrpc_response(p_id, result);
		return;
	}

	if (p_method == "asset.reimport") {
		Dictionary result;
		if (!p_params.has("paths")) {
			result["ok"] = false;
			result["error"] = "Missing paths";
			_send_jsonrpc_response(p_id, result);
			return;
		}

		Variant v = p_params["paths"];
		if (v.get_type() != Variant::ARRAY) {
			result["ok"] = false;
			result["error"] = "Invalid paths payload";
			_send_jsonrpc_response(p_id, result);
			return;
		}

		Array arr = v;
		Vector<String> paths;
		Vector<String> missing;
		for (int i = 0; i < arr.size(); i++) {
			if (arr[i].get_type() != Variant::STRING) {
				continue;
			}
			String res_path = _to_res_path(arr[i]);
			if (res_path.is_empty()) {
				continue;
			}
			if (!FileAccess::exists(res_path)) {
				missing.push_back(res_path);
				continue;
			}
			paths.push_back(res_path);
		}

		if (paths.is_empty()) {
			result["ok"] = false;
			result["error"] = "No valid paths to reimport";
			if (!missing.is_empty()) {
				Array missing_arr;
				for (int i = 0; i < missing.size(); i++) {
					missing_arr.push_back(missing[i]);
				}
				result["missing"] = missing_arr;
			}
			_send_jsonrpc_response(p_id, result);
			return;
		}

		EditorFileSystem *fs = EditorFileSystem::get_singleton();
		if (!fs) {
			result["ok"] = false;
			result["error"] = "EditorFileSystem unavailable";
			_send_jsonrpc_response(p_id, result);
			return;
		}

		fs->scan();
		fs->reimport_files(paths);

		result["ok"] = missing.is_empty();
		Array out_paths;
		for (int i = 0; i < paths.size(); i++) {
			out_paths.push_back(paths[i]);
		}
		result["paths"] = out_paths;
		if (!missing.is_empty()) {
			Array missing_arr;
			for (int i = 0; i < missing.size(); i++) {
				missing_arr.push_back(missing[i]);
			}
			result["missing"] = missing_arr;
		}
		_send_jsonrpc_response(p_id, result);
		return;
	}

	if (p_method == "scene.attachScript") {
		String scene_path = p_params.has("scenePath") ? String(p_params["scenePath"]) : String();
		String node_path = p_params.has("nodePath") ? String(p_params["nodePath"]) : String();
		String script_path = p_params.has("scriptPath") ? String(p_params["scriptPath"]) : String();
		String language = p_params.has("language") ? String(p_params["language"]) : String();

		Dictionary result;
		if (scene_path.is_empty() || node_path.is_empty() || script_path.is_empty()) {
			result["ok"] = false;
			result["error"] = "Missing scenePath/nodePath/scriptPath";
			_send_jsonrpc_response(p_id, result);
			return;
		}

		scene_path = _to_res_path(scene_path);
		script_path = _to_res_path(script_path);

		Ref<PackedScene> packed;
		bool using_open = false;
		String err_msg;
		Node *root = _get_scene_root_for_edit(scene_path, packed, using_open, err_msg);
		if (!root) {
			result["ok"] = false;
			result["error"] = err_msg;
			_send_jsonrpc_response(p_id, result);
			return;
		}

		Node *target = _find_node_for_path(root, node_path);
		if (!target) {
			if (!using_open) {
				memdelete(root);
			}
			result["ok"] = false;
			result["error"] = "Node not found";
			_send_jsonrpc_response(p_id, result);
			return;
		}

		ScriptLanguage *lang = nullptr;
		if (!language.is_empty()) {
			if (language == "gdscript") {
				lang = ScriptServer::get_language_for_extension("gd");
			} else if (language == "csharp") {
				lang = ScriptServer::get_language_for_extension("cs");
			}
		}
		if (!lang) {
			lang = ScriptServer::get_language_for_extension(script_path.get_extension());
		}
		if (!lang) {
			if (!using_open) {
				memdelete(root);
			}
			result["ok"] = false;
			result["error"] = "Script language not found";
			_send_jsonrpc_response(p_id, result);
			return;
		}

		Ref<Script> script;
		if (!FileAccess::exists(script_path)) {
			String class_name = script_path.get_basename().get_file();
			String base_type = target->get_class();
			Vector<ScriptLanguage::ScriptTemplate> templates = lang->get_built_in_templates(base_type);
			String template_content;
			if (!templates.is_empty()) {
				template_content = templates[0].content;
			}
			script = lang->make_template(template_content, class_name, base_type);
			script->set_path(script_path, true);
			Error save_err = ResourceSaver::save(script, script_path, ResourceSaver::FLAG_CHANGE_PATH);
			if (save_err != OK) {
				if (!using_open) {
					memdelete(root);
				}
				result["ok"] = false;
				result["error"] = "Failed to create script";
				_send_jsonrpc_response(p_id, result);
				return;
			}
		} else {
			Error load_err = OK;
			script = ResourceLoader::load(script_path, "", ResourceFormatLoader::CACHE_MODE_REUSE, &load_err);
			if (load_err != OK || script.is_null()) {
				if (!using_open) {
					memdelete(root);
				}
				result["ok"] = false;
				result["error"] = "Failed to load script";
				_send_jsonrpc_response(p_id, result);
				return;
			}
		}

		target->set_script(script);

		if (!_save_scene_from_root(scene_path, packed, root, using_open, err_msg)) {
			result["ok"] = false;
			result["error"] = err_msg;
			_send_jsonrpc_response(p_id, result);
			return;
		}

		result["ok"] = true;
		result["scenePath"] = scene_path;
		result["scriptPath"] = script_path;
		_send_jsonrpc_response(p_id, result);
		return;
	}

	if (p_method == "scene.connectSignal") {
		String scene_path = p_params.has("scenePath") ? String(p_params["scenePath"]) : String();
		String from_path = p_params.has("fromNodePath") ? String(p_params["fromNodePath"]) : String();
		String to_path = p_params.has("toNodePath") ? String(p_params["toNodePath"]) : String();
		String signal_name = p_params.has("signal") ? String(p_params["signal"]) : String();
		String method = p_params.has("method") ? String(p_params["method"]) : String();

		Dictionary result;
		if (scene_path.is_empty() || from_path.is_empty() || to_path.is_empty() || signal_name.is_empty() || method.is_empty()) {
			result["ok"] = false;
			result["error"] = "Missing scenePath/fromNodePath/toNodePath/signal/method";
			_send_jsonrpc_response(p_id, result);
			return;
		}

		scene_path = _to_res_path(scene_path);

		Ref<PackedScene> packed;
		bool using_open = false;
		String err_msg;
		Node *root = _get_scene_root_for_edit(scene_path, packed, using_open, err_msg);
		if (!root) {
			result["ok"] = false;
			result["error"] = err_msg;
			_send_jsonrpc_response(p_id, result);
			return;
		}

		Node *from = _find_node_for_path(root, from_path);
		Node *to = _find_node_for_path(root, to_path);
		if (!from || !to) {
			if (!using_open) {
				memdelete(root);
			}
			result["ok"] = false;
			result["error"] = "Node not found";
			_send_jsonrpc_response(p_id, result);
			return;
		}

		Callable callable = Callable(to, method);
		StringName signal = StringName(signal_name);
		if (!from->is_connected(signal, callable)) {
			Error connect_err = from->connect(signal, callable, Object::CONNECT_PERSIST);
			if (connect_err != OK) {
				if (!using_open) {
					memdelete(root);
				}
				result["ok"] = false;
				result["error"] = "Failed to connect signal";
				_send_jsonrpc_response(p_id, result);
				return;
			}
		}

		if (!_save_scene_from_root(scene_path, packed, root, using_open, err_msg)) {
			result["ok"] = false;
			result["error"] = err_msg;
			_send_jsonrpc_response(p_id, result);
			return;
		}

		result["ok"] = true;
		result["scenePath"] = scene_path;
		_send_jsonrpc_response(p_id, result);
		return;
	}

	_send_jsonrpc_error(p_id, -32601, "Method not found");
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

void EditorAIAgent::_send_jsonrpc_response(int p_id, const Dictionary &p_result) {
	if (!is_agent_connected()) {
		return;
	}
	Dictionary resp;
	resp["jsonrpc"] = "2.0";
	resp["id"] = p_id;
	resp["result"] = p_result;
	ws->send_text(JSON::stringify(resp));
}

void EditorAIAgent::_send_jsonrpc_error(int p_id, int p_code, const String &p_message) {
	if (!is_agent_connected()) {
		return;
	}
	Dictionary error;
	error["code"] = p_code;
	error["message"] = p_message;

	Dictionary resp;
	resp["jsonrpc"] = "2.0";
	resp["id"] = p_id;
	resp["error"] = error;
	ws->send_text(JSON::stringify(resp));
}

void EditorAIAgent::_handle_notification(const String &p_method, const Dictionary &p_params) {
	if (runtime_chat_active) {
		if (p_method == "status") {
			String msg = p_params.has("message") ? String(p_params["message"]) : String();
			if (msg == "chat:done") {
				runtime_chat_active = false;
			}
			return;
		}

		if (p_method == "thinking") {
			String text = p_params.has("text") ? String(p_params["text"]) : String();
			emit_signal("runtime_thinking", text);
			return;
		}

		if (p_method == "tool-call") {
			String id = p_params.has("id") ? String(p_params["id"]) : String();
			String name = p_params.has("name") ? String(p_params["name"]) : String();
			Dictionary input = p_params.has("input") ? Dictionary(p_params["input"]) : Dictionary();
			emit_signal("runtime_tool_call", id, name, input);
			return;
		}

		if (p_method == "tool-result") {
			String id = p_params.has("id") ? String(p_params["id"]) : String();
			bool ok = p_params.has("ok") ? bool(p_params["ok"]) : false;
			String name = p_params.has("name") ? String(p_params["name"]) : String();
			Dictionary output = p_params.has("output") ? Dictionary(p_params["output"]) : Dictionary();
			emit_signal("runtime_tool_result", id, ok, output);

			if (ok && (name == "writeFile" || name == "applySceneEdits" || name == "writePatch" || name == "createScene")) {
				_handle_file_written(name, output);
			}
			return;
		}

		if (p_method == "tool-progress") {
			String id = p_params.has("id") ? String(p_params["id"]) : String();
			String stage = p_params.has("stage") ? String(p_params["stage"]) : String();
			float progress = p_params.has("progress") ? float(p_params["progress"]) : -1.0f;
			emit_signal("runtime_tool_progress", id, stage, progress);
			return;
		}

		if (p_method == "chat_message") {
			String role = p_params.has("role") ? String(p_params["role"]) : String();
			String text = p_params.has("text") ? String(p_params["text"]) : String();
			emit_signal("runtime_chat_message", role, text);
			return;
		}

		if (p_method == "usage") {
			int64_t turn = 0;
			int cache_rate = 0;
			if (p_params.has("turn")) {
				Dictionary t = p_params["turn"];
				turn = t.has("totalTokens") ? int64_t(t["totalTokens"]) : 0;
			}
			if (p_params.has("session")) {
				session_usage = AITokenUsage::from_dict(p_params["session"]);
			}
			if (p_params.has("cache")) {
				Dictionary c = p_params["cache"];
				cache_rate = c.has("cacheRate") ? int(c["cacheRate"]) : 0;
			}
			emit_signal("usage_updated", turn, session_usage.total_tokens, cache_rate);
			return;
		}
	}

	if (p_method == "status") {
		String level = p_params.has("level") ? String(p_params["level"]) : "info";
		String msg = p_params.has("message") ? String(p_params["message"]) : String();
		emit_signal("status", level, msg);

		// Update processing state when chat finishes
		if (msg == "chat:done" || msg == "chat:aborted") {
			if (is_processing) {
				is_processing = false;
				emit_signal("processing_state_changed", false);
			}
		}
		return;
	}

	if (p_method == "thinking") {
		String text = p_params.has("text") ? String(p_params["text"]) : String();
		emit_signal("thinking", text);
		return;
	}

	if (p_method == "usage") {
		int64_t turn = 0;
		int cache_rate = 0;
		if (p_params.has("turn")) {
			Dictionary t = p_params["turn"];
			turn = t.has("totalTokens") ? int64_t(t["totalTokens"]) : 0;
		}
		if (p_params.has("session")) {
			session_usage = AITokenUsage::from_dict(p_params["session"]);
		}
		if (p_params.has("cache")) {
			Dictionary c = p_params["cache"];
			cache_rate = c.has("cacheRate") ? int(c["cacheRate"]) : 0;
		}
		emit_signal("usage_updated", turn, session_usage.total_tokens, cache_rate);
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
		String name = p_params.has("name") ? String(p_params["name"]) : String();
		Dictionary output = p_params.has("output") ? Dictionary(p_params["output"]) : Dictionary();
		emit_signal("tool_result", id, ok, output);

		// Auto-reload files after successful write operations
		if (ok && (name == "writeFile" || name == "applySceneEdits" || name == "writePatch" || name == "createScene")) {
			_handle_file_written(name, output);
		}
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

	if (p_method == "chat_message") {
		String role = p_params.has("role") ? String(p_params["role"]) : String();
		String text = p_params.has("text") ? String(p_params["text"]) : String();
		emit_signal("chat_message", role, text);
		return;
	}
}

void EditorAIAgent::_on_play_pressed() {
	String scene_path;
	if (EditorRunBar::get_singleton()) {
		scene_path = EditorRunBar::get_singleton()->get_playing_scene();
	}
	if (scene_path.is_empty() && EditorNode::get_singleton()) {
		Node *edited_scene = EditorNode::get_singleton()->get_edited_scene();
		if (edited_scene) {
			scene_path = edited_scene->get_scene_file_path();
		}
	}

	runtime_scene_path = scene_path;
	runtime_capture_active = true;
	runtime_errors.clear();
	runtime_warnings.clear();
	runtime_logs.clear();

	emit_signal("runtime_state_changed", "running", runtime_scene_path);
}

void EditorAIAgent::_on_stop_pressed() {
	runtime_capture_active = false;
	emit_signal("runtime_state_changed", "stopped", runtime_scene_path);

	Dictionary summary;
	summary["scenePath"] = runtime_scene_path;
	summary["errors"] = runtime_errors;
	summary["warnings"] = runtime_warnings;
	summary["logs"] = runtime_logs;
	emit_signal("runtime_summary", summary);

	if (runtime_auto_fix_enabled) {
		_request_runtime_fix_internal(false);
	}
}

void EditorAIAgent::_on_editor_log_message(const String &p_text, int p_type) {
	if (!runtime_capture_active) {
		return;
	}
	if (p_text.is_empty()) {
		return;
	}

	String level = "info";
	if (p_type == EditorLog::MSG_TYPE_ERROR) {
		level = "error";
		runtime_errors.push_back(p_text);
	} else if (p_type == EditorLog::MSG_TYPE_WARNING) {
		level = "warning";
		runtime_warnings.push_back(p_text);
	}

	runtime_logs.push_back(p_text);
	emit_signal("runtime_log_added", p_text, level);
}

void EditorAIAgent::_on_debugger_error_logged(const String &p_message, bool p_warning, const String &p_source_file, int p_source_line) {
	if (!runtime_capture_active) {
		return;
	}
	if (p_message.is_empty()) {
		return;
	}

	String message = p_message;
	if (!p_source_file.is_empty() && p_source_file.begins_with("res://")) {
		if (p_source_line >= 0) {
			message += " (" + p_source_file + ":" + itos(p_source_line) + ")";
		} else {
			message += " (" + p_source_file + ")";
		}
	}

	Vector<String> &bucket = p_warning ? runtime_warnings : runtime_errors;
	bool exists = false;
	for (const String &entry : bucket) {
		if (entry == message) {
			exists = true;
			break;
		}
	}
	if (!exists) {
		bucket.push_back(message);
	}

	runtime_logs.push_back(message);
	emit_signal("runtime_log_added", message, p_warning ? "warning" : "error");
}

String EditorAIAgent::_build_runtime_session_id() const {
	if (runtime_scene_path.is_empty()) {
		return "runtime:unknown";
	}
	return "runtime:" + runtime_scene_path;
}

void EditorAIAgent::_request_runtime_fix_internal(bool p_manual) {
	if (runtime_scene_path.is_empty()) {
		return;
	}
	if (runtime_chat_active) {
		return;
	}

	// Only proceed if there are actual runtime issues to fix
	if (runtime_errors.is_empty() && runtime_warnings.is_empty()) {
		return;
	}

	PackedStringArray lines;
	lines.push_back("Fix runtime errors from the last Play run.");
	lines.push_back("Scene: " + runtime_scene_path);
	if (p_manual) {
		lines.push_back("This was manually triggered from the Live tab.");
	}
	if (!runtime_errors.is_empty()) {
		lines.push_back("Errors:");
		for (const String &err : runtime_errors) {
			lines.push_back("- " + err);
		}
	}
	if (!runtime_warnings.is_empty()) {
		lines.push_back("Warnings:");
		for (const String &warn : runtime_warnings) {
			lines.push_back("- " + warn);
		}
	}
	lines.push_back("Focus ONLY on fixing these specific runtime errors.");
	lines.push_back("After changes, run verify on touched paths.");

	String prompt = String("\n").join(lines);

	Dictionary params;
	params["prompt"] = prompt;
	params["sessionId"] = _build_runtime_session_id();

	runtime_chat_active = true;
	send_jsonrpc("chat", params);
	emit_signal("runtime_chat_message", "You", prompt);
}

void EditorAIAgent::request_runtime_fix(bool p_manual) {
	_request_runtime_fix_internal(p_manual);
}

void EditorAIAgent::_handle_file_written(const String &p_tool_name, const Dictionary &p_output) {
	// Extract written file paths based on tool type
	Vector<String> written_paths;

	if (p_tool_name == "writeFile") {
		// writeFile output: { writeFileResult: { ok: true, path: "...", bytes: N } }
		if (p_output.has("writeFileResult")) {
			Dictionary result = p_output["writeFileResult"];
			if (result.has("path") && result.has("ok") && bool(result["ok"])) {
				written_paths.push_back(String(result["path"]));
			}
		}
	} else if (p_tool_name == "createScene") {
		// createScene output: { createSceneResult: { ok: true, path: "..." } }
		if (p_output.has("createSceneResult")) {
			Dictionary result = p_output["createSceneResult"];
			if (result.has("ok") && bool(result["ok"]) && result.has("path")) {
				written_paths.push_back(String(result["path"]));
			}
		}
	} else if (p_tool_name == "applySceneEdits") {
		// applySceneEdits output: { sceneEditResult: { ok: true, scenePath: "...", ... } }
		if (p_output.has("sceneEditResult")) {
			Dictionary result = p_output["sceneEditResult"];
			if (result.has("ok") && bool(result["ok"]) && result.has("scenePath")) {
				written_paths.push_back(String(result["scenePath"]));
			}
		}
	} else if (p_tool_name == "writePatch") {
		// writePatch output: { applyResult: { ok: true, results: [{ path: "...", wrote: bool }] } }
		if (p_output.has("applyResult")) {
			Dictionary result = p_output["applyResult"];
			if (result.has("results")) {
				Array results = result["results"];
				for (int i = 0; i < results.size(); i++) {
					Dictionary file_result = results[i];
					if (file_result.has("wrote") && bool(file_result["wrote"]) && file_result.has("path")) {
						written_paths.push_back(String(file_result["path"]));
					}
				}
			}
		}
	}

	if (written_paths.is_empty()) {
		return;
	}

	// Trigger filesystem scan to detect external changes
	if (EditorFileSystem::get_singleton()) {
		EditorFileSystem::get_singleton()->scan();
	}

	// Categorize and convert all paths to res:// format
	Vector<String> scene_paths;
	bool has_scripts = false;

	for (const String &path : written_paths) {
		if (path.is_empty()) {
			continue;
		}
		String res_path = _to_res_path(path);

		// Scenes: .tscn, .scn, .tres (packed scenes or resources that might be open)
		if (res_path.ends_with(".tscn") || res_path.ends_with(".scn")) {
			scene_paths.push_back(res_path);
		}
		// Scripts: .gd, .gdscript
		else if (res_path.ends_with(".gd") || res_path.ends_with(".gdscript")) {
			has_scripts = true;
		}
		// Shaders: .gdshader, .shader (these are also handled by script editor)
		else if (res_path.ends_with(".gdshader") || res_path.ends_with(".shader")) {
			has_scripts = true;
		}
		// Text resources that script editor can handle: .json, .txt, etc.
		else if (res_path.ends_with(".json") || res_path.ends_with(".txt") || res_path.ends_with(".cfg")) {
			has_scripts = true;
		}
	}

	// Reload scripts if any script-like files were written
	// ScriptEditor::reload_scripts() checks modification times and reloads open scripts
	if (has_scripts && ScriptEditor::get_singleton()) {
		callable_mp(ScriptEditor::get_singleton(), &ScriptEditor::reload_scripts).call_deferred(false);
	}

	// Reload scenes that are currently open in the editor
	if (!scene_paths.is_empty() && EditorNode::get_singleton()) {
		Node *edited_scene = EditorNode::get_singleton()->get_edited_scene();
		if (edited_scene) {
			String current_scene_path = edited_scene->get_scene_file_path();
			for (const String &res_path : scene_paths) {
				if (res_path == current_scene_path) {
					// Defer reload to avoid issues during message processing
					callable_mp(EditorInterface::get_singleton(), &EditorInterface::reload_scene_from_path).call_deferred(res_path);
					break;
				}
			}
		}
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

void EditorAIAgent::request_chat(const String &p_prompt, const Array &p_images) {
	Dictionary params;
	params["prompt"] = p_prompt;
	if (!p_images.is_empty()) {
		params["images"] = p_images;
	}
	send_jsonrpc("chat", params);
	emit_signal("chat_message", "You", p_prompt);

	// Update processing state
	is_processing = true;
	emit_signal("processing_state_changed", true);
}

void EditorAIAgent::abort_chat() {
	if (!is_processing) {
		return;
	}
	Dictionary params;
	send_jsonrpc("abortChat", params);
}

void EditorAIAgent::clear_session() {
	Dictionary params;
	send_jsonrpc("clearSession", params);
	session_usage = AITokenUsage();
	emit_signal("usage_updated", 0, 0, 0);
}

void EditorAIAgent::add_context_item(AIContextItemKind p_kind, const String &p_path, const String &p_label, const Dictionary &p_metadata) {
	Dictionary params;
	params["kind"] = ai_context_kind_to_string(p_kind);
	params["path"] = p_path;
	params["label"] = p_label.is_empty() ? p_path.get_file() : p_label;
	if (!p_metadata.is_empty()) {
		params["metadata"] = p_metadata;
	}
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

String EditorAIAgent::_truncate_context_text(const String &p_text, int p_max_chars) const {
	if (p_text.length() <= p_max_chars) {
		return p_text;
	}
	int start = MAX(0, p_text.length() - p_max_chars);
	String tail = p_text.substr(start);
	return "...(truncated)\n" + tail;
}

String EditorAIAgent::_build_log_context_path(const String &p_source) {
	uint64_t stamp = OS::get_singleton()->get_unix_time();
	String safe_source = p_source.is_empty() ? String("log") : p_source;
	return "log://" + safe_source + "/" + itos(stamp) + "/" + itos(log_context_seq++);
}

void EditorAIAgent::add_log_context(const String &p_label, const String &p_text, const String &p_source, const String &p_scene_path) {
	if (p_text.is_empty()) {
		return;
	}

	const int max_chars = 4000;
	String trimmed = _truncate_context_text(p_text, max_chars);

	Dictionary metadata;
	metadata["text"] = trimmed;
	if (!p_source.is_empty()) {
		metadata["source"] = p_source;
	}
	if (!p_scene_path.is_empty()) {
		metadata["scenePath"] = p_scene_path;
	}
	metadata["capturedAt"] = int64_t(OS::get_singleton()->get_unix_time());

	String label = p_label.is_empty() ? String("Runtime log") : p_label;
	add_context_item(AI_CONTEXT_LOG, _build_log_context_path(p_source), label, metadata);
}

void EditorAIAgent::run_harness(const String &p_scene_path, int p_frames) {
	Dictionary params;
	params["scenePath"] = p_scene_path;
	params["frames"] = p_frames;
	send_jsonrpc("runHarness", params);
}
