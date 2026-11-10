## Client of the lobby service (Lobby/, HTTP JSON): create / join by invite
## code, poll the lobby, squad, ready, host settings, start, leave.
class_name TfpsLobbyClient
extends Node

signal changed
signal failed(message: String)
signal match_started(address: String, join_token: String, map_name: String)

var base_url := "http://127.0.0.1:7700"
var token := ""
var lobby: Dictionary = {}
var maps: Array = []
var player_name := "Operator"
var _poll := 0.0
var _joined_token := ""


func in_lobby() -> bool:
	return token != ""


func is_host() -> bool:
	return in_lobby() and int(lobby.get("host", -1)) == int(lobby.get("you", -2))


func code() -> String:
	return lobby.get("code", "")


func host(name: String) -> void:
	player_name = name
	_send("POST", "/lobbies", {"name": name}, true)


func join(invite: String, name: String) -> void:
	player_name = name
	_send("POST", "/lobbies/join", {"code": invite.strip_edges().to_upper(), "name": name}, true)


func set_settings(settings: Dictionary) -> void:
	_send("POST", "/lobbies/%s/settings" % code(), settings)


func set_squad(squad: int) -> void:
	_send("POST", "/lobbies/%s/squad" % code(), {"squad": squad})


func set_ready(ready: bool) -> void:
	_send("POST", "/lobbies/%s/ready" % code(), {"ready": ready})


func start_match() -> void:
	_send("POST", "/lobbies/%s/start" % code(), {})


func leave() -> void:
	if in_lobby():
		_send("POST", "/lobbies/%s/leave" % code(), {})
	token = ""
	lobby = {}
	_joined_token = ""
	changed.emit()


func _process(delta: float) -> void:
	if not in_lobby():
		return
	_poll -= delta
	if _poll <= 0.0:
		_poll = 1.0
		_send("GET", "/lobbies/%s" % code(), null)


func _send(method: String, path: String, body, with_token_reply := false) -> void:
	var req := HTTPRequest.new()
	req.timeout = 5.0
	add_child(req)
	var headers := PackedStringArray(["Content-Type: application/json"])
	if token != "":
		headers.append("Authorization: Bearer %s" % token)
	var m := HTTPClient.METHOD_GET if method == "GET" else HTTPClient.METHOD_POST
	req.request_completed.connect(func(result: int, status: int, _h: PackedStringArray, data: PackedByteArray) -> void:
		req.queue_free()
		_on_reply(result, status, data, with_token_reply))
	var err := req.request(base_url + path, headers, m, JSON.stringify(body) if body != null else "")
	if err != OK:
		req.queue_free()
		failed.emit("cannot reach the lobby service at %s" % base_url)


func _on_reply(result: int, status: int, data: PackedByteArray, with_token: bool) -> void:
	if result != HTTPRequest.RESULT_SUCCESS:
		failed.emit("cannot reach the lobby service at %s" % base_url)
		return
	var v = JSON.parse_string(data.get_string_from_utf8())
	if not v is Dictionary:
		failed.emit("the lobby service sent an invalid reply")
		return
	if status >= 400:
		failed.emit(String(v.get("error", "lobby error %d" % status)))
		return
	if with_token and v.has("token"):
		token = v["token"]
	if v.has("maps"):
		maps = v["maps"]
	if v.has("lobby"):
		lobby = v["lobby"]
		changed.emit()
		var game = lobby.get("game")
		if lobby.get("status", "") == "in_game" and game is Dictionary:
			var jt: String = game.get("join_token", "")
			if jt != "" and jt != _joined_token:
				_joined_token = jt
				match_started.emit(game.get("address", ""), jt, lobby.get("settings", {}).get("map", "harbor"))
		elif lobby.get("status", "") == "open":
			_joined_token = ""
