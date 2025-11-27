// Minimal editor shim to inject the Gameable addon into any opened project.
#include "register_types.h"

#ifdef TOOLS_ENABLED

#include "core/config/project_settings.h"
#include "core/io/json.h"
#include "core/io/resource_loader.h"
#include "core/os/os.h"
#include "core/string/print_string.h"
#include "core/templates/list.h"
#include "editor/docks/editor_dock_manager.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/plugins/editor_plugin.h"
#include "editor/settings/editor_settings.h"
#include "modules/websocket/websocket_peer.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/control.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/rich_text_label.h"
#include "scene/gui/tab_container.h"
#include "scene/main/node.h"
#include "scene/main/timer.h"

namespace {

class GameableDock final : public PanelContainer {
	GDCLASS(GameableDock, PanelContainer);
	static void _bind_methods() {}

	VBoxContainer *root = nullptr;
	RichTextLabel *log = nullptr;
	LineEdit *input = nullptr;
	Button *send = nullptr;
	WebSocketPeer *ws = nullptr;
	String ws_url;
	Label *status = nullptr;
	Timer *poll_timer = nullptr;
	double retry_delay = 0.5;
	uint64_t next_retry_msec = 0;
	bool sent_hello = false;
	bool sent_context = false;
	uint64_t last_context_sent_msec = 0;
	String last_context_fingerprint;
	int last_ready_state = -1;
	// No confirmation UI; agent-initiated applies are immediate.
	RichTextLabel *bottom_logs = nullptr;
	Button *run_harness_btn = nullptr;

	// Phase 1 UI: token meter, tool-progress, reasoning pane
	Label *token_meter = nullptr;
	int64_t turn_tokens = 0;
	int64_t session_tokens = 0;
	int tool_call_count = 0;

	// Tool-progress tracking: tool_id -> { name, stage, progress }
	VBoxContainer *tool_progress_container = nullptr;
	HashMap<String, HBoxContainer *> active_tool_rows;
	HashMap<String, uint64_t> tool_result_times; // for delayed removal

	// Collapsible reasoning pane
	VBoxContainer *reasoning_section = nullptr;
	Button *reasoning_toggle = nullptr;
	RichTextLabel *reasoning_text = nullptr;
	bool reasoning_collapsed = true;
	String current_reasoning; // accumulates thinking text for current turn

public:
	GameableDock() {
		set_name("Gameable");
		set_process(true); // Ensure we poll even if the timer hasn't started yet
		root = memnew(VBoxContainer);
		add_child(root);

		// Token meter row at top
		HBoxContainer *meter_row = memnew(HBoxContainer);
		root->add_child(meter_row);
		token_meter = memnew(Label);
		token_meter->set_text("Tokens: — | Session: —");
		token_meter->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		meter_row->add_child(token_meter);

		// New conversation button
		Button *new_conv_btn = memnew(Button);
		new_conv_btn->set_text("New");
		new_conv_btn->set_tooltip_text("Start new conversation");
		meter_row->add_child(new_conv_btn);
		new_conv_btn->connect("pressed", callable_mp(this, &GameableDock::_on_new_conversation));

		// Tool-progress container (shows active tools)
		tool_progress_container = memnew(VBoxContainer);
		root->add_child(tool_progress_container);

		log = memnew(RichTextLabel);
		log->set_autowrap_mode(TextServer::AUTOWRAP_WORD);
		log->set_v_size_flags(Control::SIZE_EXPAND_FILL);
		root->add_child(log);
		log->set_meta_underline(true);
		log->connect("meta_clicked", callable_mp(this, &GameableDock::_on_meta_clicked));

		// Collapsible reasoning pane (below chat log)
		reasoning_section = memnew(VBoxContainer);
		root->add_child(reasoning_section);

		reasoning_toggle = memnew(Button);
		reasoning_toggle->set_text("▶ Reasoning");
		reasoning_toggle->set_text_alignment(HORIZONTAL_ALIGNMENT_LEFT);
		reasoning_section->add_child(reasoning_toggle);
		reasoning_toggle->connect("pressed", callable_mp(this, &GameableDock::_toggle_reasoning));

		reasoning_text = memnew(RichTextLabel);
		reasoning_text->set_autowrap_mode(TextServer::AUTOWRAP_WORD);
		reasoning_text->set_custom_minimum_size(Size2(0, 0));
		reasoning_text->set_v_size_flags(Control::SIZE_SHRINK_BEGIN);
		reasoning_text->set_visible(false); // collapsed by default
		reasoning_text->add_theme_font_override("normal_font", get_theme_font("source", "EditorFonts"));
		reasoning_section->add_child(reasoning_text);

		HBoxContainer *status_row = memnew(HBoxContainer);
		root->add_child(status_row);
		status = memnew(Label);
		status_row->add_child(status);
		Button *reconnect_btn = memnew(Button);
		reconnect_btn->set_text("Reconnect");
		status_row->add_child(reconnect_btn);
		reconnect_btn->connect("pressed", callable_mp(this, &GameableDock::_reconnect));

		run_harness_btn = memnew(Button);
		run_harness_btn->set_text("Run Scene");
		status_row->add_child(run_harness_btn);
		run_harness_btn->connect("pressed", callable_mp(this, &GameableDock::_on_run_scene));

		HBoxContainer *row = memnew(HBoxContainer);
		root->add_child(row);

		input = memnew(LineEdit);
		input->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		row->add_child(input);

		send = memnew(Button);
		send->set_text("Send");
		row->add_child(send);

		send->connect("pressed", callable_mp(this, &GameableDock::_on_send));
		input->connect("text_submitted", callable_mp(this, &GameableDock::_on_submit));

		ws_url = EDITOR_GET("gameable/ws_url");
		_reconnect();

		poll_timer = memnew(Timer);
		poll_timer->set_wait_time(0.1);
		poll_timer->set_one_shot(false);
		poll_timer->set_autostart(true);
		add_child(poll_timer);
		poll_timer->connect("timeout", callable_mp(this, &GameableDock::_on_poll));

		// Listen to editor selection changes to push context updates.
		if (EditorSelection *es = EditorNode::get_singleton()->get_editor_selection()) {
			es->connect("selection_changed", callable_mp(this, &GameableDock::_on_selection_changed));
		}
	}

	void _toggle_reasoning() {
		reasoning_collapsed = !reasoning_collapsed;
		reasoning_text->set_visible(!reasoning_collapsed);
		reasoning_toggle->set_text(reasoning_collapsed ? "▶ Reasoning" : "▼ Reasoning");
		if (!reasoning_collapsed) {
			// Limit max height when expanded
			reasoning_text->set_custom_minimum_size(Size2(0, MIN(240.0, reasoning_text->get_content_height())));
		}
	}

	void _on_new_conversation() {
		// Clear local UI state
		log->clear();
		reasoning_text->clear();
		current_reasoning = "";
		turn_tokens = 0;
		tool_call_count = 0;
		_update_token_meter();

		// Clear active tool rows
		for (KeyValue<String, HBoxContainer *> &kv : active_tool_rows) {
			kv.value->queue_free();
		}
		active_tool_rows.clear();
		tool_result_times.clear();

		// Send clearSession RPC to agent
		Dictionary params;
		_send_jsonrpc("clearSession", params);
		_log_output("New conversation started.");
	}

	void _update_token_meter() {
		String text = "Turn: " + itos(turn_tokens) + " | Session: " + itos(session_tokens);
		if (tool_call_count > 0) {
			text += " | Tools: " + itos(tool_call_count);
		}
		token_meter->set_text(text);
	}

	void _add_tool_progress(const String &p_id, const String &p_name, const String &p_stage, double p_progress = -1.0) {
		HBoxContainer *row = nullptr;
		if (active_tool_rows.has(p_id)) {
			row = active_tool_rows[p_id];
			// Update existing row
			for (int i = 0; i < row->get_child_count(); i++) {
				if (Label *lbl = Object::cast_to<Label>(row->get_child(i))) {
					lbl->set_text(p_name + ": " + p_stage);
					break;
				}
			}
		} else {
			// Create new row
			row = memnew(HBoxContainer);
			Label *lbl = memnew(Label);
			lbl->set_text(p_name + ": " + p_stage);
			lbl->set_h_size_flags(Control::SIZE_EXPAND_FILL);
			row->add_child(lbl);
			tool_progress_container->add_child(row);
			active_tool_rows[p_id] = row;
		}
	}

	void _remove_tool_progress(const String &p_id) {
		if (active_tool_rows.has(p_id)) {
			active_tool_rows[p_id]->queue_free();
			active_tool_rows.erase(p_id);
		}
		tool_result_times.erase(p_id);
	}

	void _check_tool_result_cleanup() {
		// Remove tool rows 1 second after their result arrived
		uint64_t now = OS::get_singleton()->get_ticks_msec();
		Vector<String> to_remove;
		for (const KeyValue<String, uint64_t> &kv : tool_result_times) {
			if (now - kv.value > 1000) {
				to_remove.push_back(kv.key);
			}
		}
		for (const String &id : to_remove) {
			_remove_tool_progress(id);
		}
	}

	void _process(double p_delta) {
		// Extra safety: poll while connecting so status flips without user interaction
		if (ws && ws->get_ready_state() != WebSocketPeer::STATE_OPEN) {
			ws->poll();
		}
	}

	void set_bottom_logs(RichTextLabel *p_logs) { bottom_logs = p_logs; }

	void _on_send() { _append_and_clear(); }
	void _on_submit(const String &p_text) { _append_and_clear(); }

	void _on_meta_clicked(const Variant &p_meta) {
		if (p_meta.get_type() != Variant::STRING) {
			return;
		}
		String p = p_meta;
		if (p.is_empty()) {
			return;
		}
		if (p.ends_with(".tscn")) {
			EditorInterface::get_singleton()->open_scene_from_path(p);
			return;
		}
		Ref<Resource> res = ResourceLoader::load(p);
		if (res.is_valid()) {
			EditorInterface::get_singleton()->edit_resource(res);
			return;
		}
		EditorInterface::get_singleton()->select_file(p);
	}

	void _send_jsonrpc(const String &p_method, const Dictionary &p_params) {
		if (!ws || ws->get_ready_state() != WebSocketPeer::STATE_OPEN) {
			return;
		}
		Dictionary req;
		req["jsonrpc"] = "2.0";
		req["id"] = (int)OS::get_singleton()->get_ticks_msec();
		req["method"] = p_method;
		req["params"] = p_params;
		ws->send_text(JSON::stringify(req));
	}

	void _log_output(const String &p_msg) {
		print_line("[Gameable] ", p_msg);
		if (bottom_logs) {
			bottom_logs->append_text(p_msg + "\n");
		}
	}

	bool _send_context_snapshot(bool p_force = false) {
		Dictionary params;
		params["projectRoot"] = ProjectSettings::get_singleton()->get_resource_path();
		if (Node *scene_root = EditorNode::get_singleton()->get_edited_scene()) {
			String scene_path = scene_root->get_scene_file_path();
			if (!scene_path.is_empty()) {
				params["scenePath"] = scene_path;
			}
		}
		String current_path = EditorInterface::get_singleton()->get_current_path();
		if (!current_path.is_empty()) {
			params["currentPath"] = current_path;
		}
		PackedStringArray sel;
		if (EditorSelection *es = EditorNode::get_singleton()->get_editor_selection()) {
			List<Node *> nodes = es->get_full_selected_node_list();
			for (List<Node *>::Element *E = nodes.front(); E; E = E->next()) {
				Node *n = E->get();
				if (n) {
					sel.push_back(String(n->get_path()));
				}
			}
		}
		if (sel.size() > 0) {
			params["selection"] = sel;
		}
		Array unsaved;
		// TODO: collect actual unsaved buffers from ScriptEditor when available.
		// Keeping this empty for now unless we can access the current editor buffer safely.
		if (unsaved.size() > 0) {
			params["unsavedBuffers"] = unsaved;
		}

		// Fingerprint to avoid sending identical snapshots repeatedly.
		String fingerprint;
		fingerprint += String(params.has("projectRoot") ? String(params["projectRoot"]) : String());
		fingerprint += String(":") + String(params.has("scenePath") ? String(params["scenePath"]) : String());
		fingerprint += String(":") + String(params.has("currentPath") ? String(params["currentPath"]) : String());
		if (params.has("selection")) {
			PackedStringArray s = params["selection"];
			for (int i = 0; i < s.size(); i++) {
				fingerprint += String(":") + s[i];
			}
		}
		if (!p_force && fingerprint == last_context_fingerprint) {
			return false;
		}
		_send_jsonrpc("context", params);
		sent_context = true;
		last_context_sent_msec = OS::get_singleton()->get_ticks_msec();
		last_context_fingerprint = fingerprint;
		return true;
	}

	void _on_selection_changed() {
		if (ws && ws->get_ready_state() == WebSocketPeer::STATE_OPEN) {
			_send_context_snapshot();
		}
	}

	void _append_and_clear() {
		const String t = input->get_text().strip_edges();
		if (t.is_empty()) {
			return;
		}
		// Reset turn-level state before sending
		turn_tokens = 0;
		tool_call_count = 0;
		current_reasoning = "";
		reasoning_text->clear();
		_update_token_meter();

		log->append_text("[b]You:[/b] " + t + "\n");
		Dictionary req;
		req["jsonrpc"] = "2.0";
		req["id"] = (int)OS::get_singleton()->get_ticks_msec();
		req["method"] = "chat";
		Dictionary params;
		params["prompt"] = t;
		req["params"] = params;
		const String payload = JSON::stringify(req);
		if (ws && ws->get_ready_state() == WebSocketPeer::STATE_OPEN) {
			ws->send_text(payload);
		} else if (ws) {
			ws->poll();
		}
		input->clear();
	}

	void _on_run_scene() {
		String scene_path;
		if (Node *scene_root = EditorNode::get_singleton()->get_edited_scene()) {
			scene_path = scene_root->get_scene_file_path();
		}
		if (scene_path.is_empty()) {
			scene_path = EditorInterface::get_singleton()->get_current_path();
		}
		if (scene_path.is_empty()) {
			if (bottom_logs) {
				bottom_logs->append_text("[warn] No scene path to run.\n");
			}
			return;
		}
		Dictionary params;
		params["scenePath"] = scene_path;
		_send_jsonrpc("runHarness", params);
	}

	void _set_status(const String &p_text) {
		if (status) {
			status->set_text(p_text);
		}
	}

	void _reconnect() {
		if (ws) {
			ws->close();
			memdelete(ws);
			ws = nullptr;
		}
		ws = WebSocketPeer::create();
		_set_status("Connecting...");
		_log_output(String("WS connecting to ") + ws_url);
		if (!ws) {
			_set_status("WebSocket unsupported in this build");
			return;
		}
		Error err = ws->connect_to_url(ws_url);
		if (err != OK) {
			_set_status("Connect error: " + itos(err));
			_log_output(String("WS connect error: ") + itos(err));
			retry_delay = MIN(retry_delay * 2.0, 5.0);
			next_retry_msec = OS::get_singleton()->get_ticks_msec() + uint64_t(retry_delay * 1000.0);
		} else {
			retry_delay = 0.5;
		}
	}

	void _on_poll() {
		if (!ws) {
			return;
		}
		ws->poll();
		int rs = ws->get_ready_state();
		if (rs != last_ready_state) {
			last_ready_state = rs;
			String rs_text = "";
			switch (rs) {
				case WebSocketPeer::STATE_CONNECTING:
					rs_text = "STATE_CONNECTING";
					break;
				case WebSocketPeer::STATE_OPEN:
					rs_text = "STATE_OPEN";
					break;
				case WebSocketPeer::STATE_CLOSING:
					rs_text = "STATE_CLOSING";
					break;
				case WebSocketPeer::STATE_CLOSED:
					rs_text = "STATE_CLOSED";
					break;
			}
			_log_output(String("WS state -> ") + rs_text);
		}
		switch (rs) {
			case WebSocketPeer::STATE_OPEN: {
				_set_status("Connected");
				if (!sent_hello) {
					Dictionary hello;
					hello["session"] = "dev";
					_send_jsonrpc("hello", hello);
					sent_hello = true;
				}
				if (!sent_context) {
					_send_context_snapshot(true);
				}
				while (ws->get_available_packet_count() > 0) {
					const uint8_t *buf = nullptr;
					int len = 0;
					if (ws->get_packet(&buf, len) == OK && buf && len > 0) {
						String text = ws->was_string_packet() ? String::utf8((const char *)buf, len) : "<binary:" + itos(len) + " bytes>";
						Variant parsed = JSON::parse_string(text);
						if (parsed.get_type() == Variant::DICTIONARY) {
							Dictionary d = parsed;
							// If server hello (non-JSON-RPC), mark connected immediately
							if (d.has("type") && String(d["type"]) == String("hello")) {
								_set_status("Connected");
							}
							if (d.has("method") && !d.has("id") && d.has("params")) {
								String method = d["method"];
								Dictionary params = d["params"];
								if (method == "status") {
									String level = params.has("level") ? String(params["level"]) : String("info");
									String msg = params.has("message") ? String(params["message"]) : String();
									// Reset turn state on chat:start
									if (msg == "chat:start") {
										turn_tokens = 0;
										tool_call_count = 0;
										current_reasoning = "";
										reasoning_text->clear();
										_update_token_meter();
									}
									if (bottom_logs) {
										bottom_logs->append_text("[" + level + "] " + msg + "\n");
									}
									continue;
								}
								if (method == "usage") {
									// Update token meter from usage event
									if (params.has("turn")) {
										Dictionary turn = params["turn"];
										turn_tokens = turn.has("totalTokens") ? int64_t(turn["totalTokens"]) : 0;
									}
									if (params.has("session")) {
										Dictionary sess = params["session"];
										session_tokens = sess.has("totalTokens") ? int64_t(sess["totalTokens"]) : 0;
									}
									_update_token_meter();
									continue;
								}
								if (method == "thinking") {
									String t = params.has("text") ? String(params["text"]) : String();
									// Append to reasoning pane instead of main log
									current_reasoning += t;
									reasoning_text->append_text(t);
									continue;
								}
								if (method == "tool-call") {
									String id = params.has("id") ? String(params["id"]) : String();
									String name = params.has("name") ? String(params["name"]) : String();
									tool_call_count++;
									_update_token_meter();
									// Add to tool progress
									_add_tool_progress(id, name, "running");
									log->append_text("[b]" + name + "[/b] ...\n");
									continue;
								}
								if (method == "tool-progress") {
									String id = params.has("id") ? String(params["id"]) : String();
									String stage = params.has("stage") ? String(params["stage"]) : String();
									String note = params.has("note") ? String(params["note"]) : String();
									double progress = params.has("progress") ? double(params["progress"]) : -1.0;
									// Find tool name from active rows (or use stage as fallback)
									_add_tool_progress(id, stage, note.is_empty() ? stage : note, progress);
									continue;
								}
								if (method == "tool-result") {
									String id = params.has("id") ? String(params["id"]) : String();
									// Mark for delayed removal
									tool_result_times[id] = OS::get_singleton()->get_ticks_msec();
									// Render clickable search results
									if (params.has("name") && String(params["name"]) == "searchFiles" && params.has("output")) {
										Dictionary out = params["output"]; // expected { hits: Array<{ path, line, preview }> }
										if (out.has("hits")) {
											Array hits = out["hits"];
											for (int i = 0; i < hits.size(); i++) {
												Dictionary h = hits[i];
												if (h.has("path")) {
													String p = h["path"];
													String preview = h.has("preview") ? String(h["preview"]) : String();
													log->append_text(" • ");
													log->push_meta(p);
													log->append_text(p);
													log->pop();
													log->append_text(" — " + preview + "\n");
													// Note: Godot's RichTextLabel doesn't support click events per range without meta;
													// if we switch to BBCode and meta later, we can open files on click. For now, just list.
												}
											}
											continue;
										}
									}
									log->append_text("[b]tool-result:[/b] " + String(JSON::stringify(params)) + "\n");
									continue;
								}
								if (method == "diff-preview") {
									log->append_text("[b]diff-preview:[/b] " + String(JSON::stringify(params)) + "\n");
									continue;
								}
							}
						}
						log->append_text("[b]Agent:[/b] " + text + "\n");
					}
				}
				// Periodic context refresh (throttled)
				uint64_t now = OS::get_singleton()->get_ticks_msec();
				if (now - last_context_sent_msec > 1500) {
					_send_context_snapshot();
				}
				// Cleanup completed tool progress rows after 1s delay
				_check_tool_result_cleanup();
			} break;
			case WebSocketPeer::STATE_CONNECTING: {
				_set_status("Connecting...");
			} break;
			case WebSocketPeer::STATE_CLOSING: {
				_set_status("Closing...");
			} break;
			case WebSocketPeer::STATE_CLOSED: {
				uint64_t now = OS::get_singleton()->get_ticks_msec();
				if (now >= next_retry_msec) {
					_set_status("Reconnecting...");
					_reconnect();
				}
			} break;
		}
	}
};

class GameableEditorBuiltin final : public EditorPlugin {
	GDCLASS(GameableEditorBuiltin, EditorPlugin);
	static void _bind_methods() {}

	Control *chat_dock = nullptr;
	Control *bottom_logs = nullptr;
	Button *bottom_toggle_btn = nullptr;

	void _ensure_dock_first() {
		if (!chat_dock) {
			return;
		}
		Node *parent = chat_dock->get_parent();
		while (parent && Object::cast_to<TabContainer>(parent) == nullptr) {
			parent = parent->get_parent();
		}
		if (TabContainer *tabs = Object::cast_to<TabContainer>(parent)) {
			if (tabs->get_child_count() > 0 && tabs->get_child(0) != chat_dock) {
				tabs->move_child(chat_dock, 0);
			}
		}
	}

public:
	GameableEditorBuiltin() = default;
	~GameableEditorBuiltin() override = default;

	void _notification(int p_what) {
		switch (p_what) {
			case NOTIFICATION_ENTER_TREE: {
				// Editor settings defaults
				EDITOR_DEF("gameable/enable", true);
				EDITOR_DEF("gameable/ws_url", "ws://127.0.0.1:1999/session/dev");
				if (!bool(EDITOR_GET("gameable/enable"))) {
					return;
				}
				chat_dock = memnew(GameableDock);
				add_control_to_dock(DOCK_SLOT_RIGHT_UL, chat_dock);
				_ensure_dock_first();
				EditorDockManager::get_singleton()->focus_dock(chat_dock);

				// Bottom panel logs tab
				RichTextLabel *logs = memnew(RichTextLabel);
				logs->set_autowrap_mode(TextServer::AUTOWRAP_WORD);
				logs->set_v_size_flags(Control::SIZE_EXPAND_FILL);
				bottom_logs = logs;
				bottom_toggle_btn = add_control_to_bottom_panel(bottom_logs, "Gameable");
				if (GameableDock *dock = Object::cast_to<GameableDock>(chat_dock)) {
					dock->set_bottom_logs(Object::cast_to<RichTextLabel>(bottom_logs));
				}
			} break;
			case NOTIFICATION_EXIT_TREE: {
				if (chat_dock) {
					remove_control_from_docks(chat_dock);
					chat_dock->queue_free();
				}
				if (bottom_logs) {
					remove_control_from_bottom_panel(bottom_logs);
					bottom_logs->queue_free();
				}
				chat_dock = nullptr;
				bottom_logs = nullptr;
				bottom_toggle_btn = nullptr;
			} break;
		}
	}
};

static void editor_init_callback() {
	// Removed project-level injection. Always load built-in plugin.
	EditorNode::get_singleton()->add_editor_plugin(memnew(GameableEditorBuiltin));
}

} // namespace

#endif // TOOLS_ENABLED

void initialize_gameable_editor_plugin() {
#ifdef TOOLS_ENABLED
	EditorNode::add_init_callback(editor_init_callback);
#endif
}

void uninitialize_gameable_editor_plugin() {}
