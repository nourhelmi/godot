// Minimal editor shim to inject the Gameable addon into any opened project.
#include "register_types.h"

#ifdef TOOLS_ENABLED

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "editor/editor_node.h"
#include "editor/editor_settings.h"
#include "editor/editor_dock_manager.h"
#include "editor/plugins/editor_plugin.h"
#include "scene/gui/control.h"
#include "scene/gui/tab_container.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/box_container.h"
#include "scene/gui/rich_text_label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "modules/websocket/websocket_peer.h"

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
public:
	GameableDock() {
		set_name("Gameable");
		root = memnew(VBoxContainer);
		add_child(root);

		log = memnew(RichTextLabel);
		log->set_autowrap_mode(TextServer::AUTOWRAP_WORD);
		log->set_v_size_flags(SIZE_EXPAND_FILL);
		root->add_child(log);

		HBoxContainer *status_row = memnew(HBoxContainer);
		root->add_child(status_row);
		status = memnew(Label);
		status_row->add_child(status);
		Button *reconnect_btn = memnew(Button);
		reconnect_btn->set_text("Reconnect");
		status_row->add_child(reconnect_btn);
		reconnect_btn->connect("pressed", callable_mp(this, &GameableDock::_reconnect));

		HBoxContainer *row = memnew(HBoxContainer);
		root->add_child(row);

		input = memnew(LineEdit);
		input->set_h_size_flags(SIZE_EXPAND_FILL);
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
		add_child(poll_timer);
		poll_timer->connect("timeout", callable_mp(this, &GameableDock::_on_poll));
		poll_timer->start();
	}

	void _on_send() { _append_and_clear(); }
	void _on_submit(const String &p_text) { _append_and_clear(); }

	void _append_and_clear() {
		const String t = input->get_text().strip_edges();
		if (t.is_empty()) {
			return;
		}
		log->append_text("[b]You:[/b] " + t + "\n");
		if (ws && ws->get_ready_state() == WebSocketPeer::STATE_OPEN) {
			ws->send_text(t);
		} else if (ws) {
			ws->poll();
		}
		input->clear();
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
		if (!ws) {
			_set_status("WebSocket unsupported in this build");
			return;
		}
		Error err = ws->connect_to_url(ws_url);
		if (err != OK) {
			_set_status("Connect error: " + itos(err));
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
		switch (ws->get_ready_state()) {
			case WebSocketPeer::STATE_OPEN: {
				_set_status("Connected");
				while (ws->get_available_packet_count() > 0) {
					const uint8_t *buf = nullptr;
					int len = 0;
					if (ws->get_packet(&buf, len) == OK && buf && len > 0) {
						String text = ws->was_string_packet() ? String::utf8((const char *)buf, len) : "<binary:" + itos(len) + " bytes>";
						log->append_text("[b]Agent:[/b] " + text + "\n");
					}
				}
			} break;
			case WebSocketPeer::STATE_CLOSED:
			case WebSocketPeer::STATE_CONNECTING:
			case WebSocketPeer::STATE_CLOSING: {
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
				// Make Gameable the active tab immediately.
				EditorDockManager::get_singleton()->focus_dock(chat_dock);
			} break;
			case NOTIFICATION_EXIT_TREE: {
				if (chat_dock) {
					remove_control_from_docks(chat_dock);
					chat_dock->queue_free();
				}
				chat_dock = nullptr;
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


