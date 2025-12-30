/**************************************************************************/
/*  editor_ai_agent.cpp                                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GAMEABLE                                   */
/*                     AI-powered game development                        */
/**************************************************************************/

#include "editor_ai_agent.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/io/resource_loader.h"
#include "core/object/script_language.h"
#include "core/os/os.h"
#include "core/string/char_utils.h"
#include "core/string/print_string.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/settings/editor_settings.h"
#include "modules/websocket/websocket_peer.h"
#include "scene/main/timer.h"
#include "scene/main/node.h"
#include "scene/resources/packed_scene.h"
#include "servers/rendering_server.h"
#include "servers/rendering/shader_language.h"
#include "servers/rendering/shader_preprocessor.h"
#include "servers/rendering/shader_types.h"

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
		String name = p_params.has("name") ? String(p_params["name"]) : String();
		Dictionary output = p_params.has("output") ? Dictionary(p_params["output"]) : Dictionary();
		emit_signal("tool_result", id, ok, output);

		// Auto-reload files after successful write operations
		if (ok && (name == "writeFile" || name == "applySceneEdits" || name == "writePatch")) {
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

	// Check if any written file is the currently open scene - if so, reload it
	if (EditorNode::get_singleton()) {
		Node *edited_scene = EditorNode::get_singleton()->get_edited_scene();
		if (edited_scene) {
			String current_scene_path = edited_scene->get_scene_file_path();
			for (const String &path : written_paths) {
				if (!path.is_empty() && path.ends_with(".tscn")) {
					// Convert absolute path to res:// if needed
					String res_path = path;
					if (path.begins_with("/")) {
						String project_path = ProjectSettings::get_singleton()->get_resource_path();
						if (path.begins_with(project_path)) {
							res_path = "res://" + path.substr(project_path.length() + 1);
						}
					}
					if (res_path == current_scene_path) {
						// Defer reload to avoid issues during message processing
						callable_mp(EditorInterface::get_singleton(), &EditorInterface::reload_scene_from_path).call_deferred(res_path);
						break;
					}
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
