## Entry point: menus over a live map backdrop, and matches.
## Command line (after "--"): --armor 0..2 (medium, light, heavy) and
##   --primary ID (weapons.json id) also apply
##   --connect host:port --map NAME [--name NAME]   join a server directly
##   --lobby http://host:7700 [--invite CODE]       lobby service (Play launcher)
##   --quality 0..3
## Test options: --shot PATH --after SECONDS [--ready (on joining a lobby) --yaw D --pitch D --fire --walk --aim --reload --slot N --menu SCREEN]
extends Node

var args := {}
var settings: TfpsSettings
var net: TfpsNet
var game: TfpsGame
var lobby: TfpsLobbyClient
var menu: TfpsMenu
var backdrop: Node3D
var _menu_layer: CanvasLayer
var _shot_at := -1.0
var _orbit := 0.0
var _backdrop_cam: Camera3D
var _hosting := false


func _ready() -> void:
	args = _parse(OS.get_cmdline_user_args())
	settings = TfpsSettings.load_or_default()
	if args.has("quality"):
		settings.quality = clampi(int(args["quality"]), 0, 3)
	if args.has("name"):
		settings.player_name = args["name"]
	if args.has("lobby"):
		settings.lobby_url = args["lobby"]
	TfpsWorld.quality = settings.quality
	TfpsControls.install(settings.keys)
	net = TfpsNet.new()
	lobby = TfpsLobbyClient.new()
	lobby.base_url = settings.lobby_url
	add_child(lobby)
	lobby.match_started.connect(_on_lobby_match)
	_menu_layer = CanvasLayer.new()
	_menu_layer.layer = 10
	add_child(_menu_layer)
	menu = TfpsMenu.new()
	menu.settings = settings
	menu.lobby = lobby
	_menu_layer.add_child(menu)
	menu.practice.connect(_on_practice)
	menu.host_lobby.connect(_on_host)
	menu.join_lobby.connect(func(code: String) -> void:
		lobby.base_url = settings.lobby_url
		lobby.join(code, settings.player_name)
		_wait_for_lobby())
	menu.direct_connect.connect(func(address: String, map_name: String) -> void: _start_match(address, map_name, "0"))
	menu.resume.connect(_resume)
	menu.leave_match.connect(_leave_match)
	menu.settings_changed.connect(_apply_settings)
	menu.quit_game.connect(func() -> void: get_tree().quit())
	if net.native == null:
		menu.show_error("The game's native extension did not load (%s)." % net.error)
	if args.has("connect"):
		_start_match(args["connect"], args.get("map", "test_arena"), "0")
	else:
		_show_backdrop(settings.last_map)
		if args.has("invite"):
			lobby.join(args["invite"], settings.player_name)
			_wait_for_lobby()
		if args.has("menu"):
			menu.push(args["menu"])
		if args.has("test-host"):
			_on_host()
	if args.has("shot"):
		_shot_at = float(args.get("after", "6"))


func _parse(a: PackedStringArray) -> Dictionary:
	var out := {}
	var i := 0
	while i < a.size():
		var k := a[i].trim_prefix("--")
		if i + 1 < a.size() and not a[i + 1].begins_with("--"):
			out[k] = a[i + 1]
			i += 2
		else:
			out[k] = "true"
			i += 1
	return out


## The menu background: the map with its lighting and a slow camera drift.
func _show_backdrop(map_name: String) -> void:
	if backdrop:
		backdrop.queue_free()
	backdrop = Node3D.new()
	add_child(backdrop)
	var bnet := TfpsNet.new()
	if bnet.load_map(map_name):
		var w := TfpsWorld.new()
		backdrop.add_child(w)
		w.build(bnet.world, bnet)
	_backdrop_cam = Camera3D.new()
	_backdrop_cam.fov = 70.0
	backdrop.add_child(_backdrop_cam)
	_backdrop_cam.make_current()
	Input.mouse_mode = Input.MOUSE_MODE_VISIBLE


func _process(delta: float) -> void:
	if _backdrop_cam and is_instance_valid(_backdrop_cam) and game == null:
		_orbit += delta * 0.03
		_backdrop_cam.position = Vector3(cos(_orbit) * 58.0, 24.0, sin(_orbit) * 50.0)
		_backdrop_cam.look_at(Vector3(4, 0, 0))
	if _shot_at >= 0.0:
		_test_input()


func _unhandled_input(event: InputEvent) -> void:
	if event.is_action_pressed("menu") and game != null:
		if menu.visible:
			menu.back()
		else:
			_pause()
		get_viewport().set_input_as_handled()
	if event.is_action_pressed("net_stats") and game != null:
		game.hud.show_net = not game.hud.show_net


func _on_practice(map_name: String, bots: int, skill := 0.5) -> void:
	var port := 27777 + randi() % 1000
	if not TfpsLocalServices.start_practice(map_name, bots, port, skill):
		menu.show_error("Could not start the local server (the Server folder is missing).")
		return
	_start_match("127.0.0.1:%d" % port, map_name, "0")


func _on_host() -> void:
	lobby.base_url = settings.lobby_url
	# Hosting on this computer: start the bundled lobby service if none answers yet.
	if settings.lobby_url.contains("127.0.0.1") or settings.lobby_url.contains("localhost"):
		if not _hosting and TfpsLocalServices.available():
			TfpsLocalServices.start_lobby(TfpsLocalServices.lan_address())
			_hosting = true
	lobby.host(settings.player_name)
	_wait_for_lobby()


func _wait_for_lobby() -> void:
	menu.show_error("Contacting the lobby service...")
	var on_change := func() -> void:
		if lobby.in_lobby() and menu.current() != "lobby" and game == null:
			menu.open("lobby")
		if lobby.in_lobby() and args.get("ready", "") == "true":   # test option: ready up on joining
			args["ready"] = "sent"
			lobby.set_ready(true)
	if not lobby.changed.is_connected(on_change):
		lobby.changed.connect(on_change)


func _on_lobby_match(address: String, join_token: String, map_name: String) -> void:
	_start_match(address, map_name, join_token)


func _start_match(address: String, map_name: String, join_token: String) -> void:
	if not net.load_map(map_name):
		menu.show_error(net.error)
		return
	if backdrop:
		backdrop.queue_free()
		backdrop = null
		_backdrop_cam = null
	if game:
		game.leave()
		game.queue_free()
	game = TfpsGame.new()
	add_child(game)
	game.left_match.connect(_on_left_match)
	var armor := int(args.get("armor", str(settings.armor)))
	var att := int(args.get("attachments", str(settings.attachments())))
	var primary := int(args.get("primary", str(settings.primary)))
	if not game.start(net, address, settings.player_name, join_token, armor, att, primary):
		menu.show_error(net.error)
		return
	_apply_settings()
	menu.visible = false
	menu.in_match = true
	if not args.has("shot"):
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED


func _apply_settings() -> void:
	TfpsWorld.quality = settings.quality
	lobby.base_url = settings.lobby_url
	if game and game.player:
		game.player.sensitivity = settings.sensitivity
		game.player.ads_sensitivity = settings.ads_sensitivity
		game.player.invert_y = settings.invert_y
		game.player.toggle_crouch = settings.toggle_crouch
		game.player.base_fov = settings.fov
		game.audio.master_volume = settings.master_volume
		game.audio.effects_volume = settings.effects_volume


func _pause() -> void:
	menu.visible = true
	menu.open("pause")
	game.paused = true
	Input.mouse_mode = Input.MOUSE_MODE_VISIBLE


func _resume() -> void:
	menu.visible = false
	game.paused = false
	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED


func _leave_match() -> void:
	_end_match("")


func _on_left_match(reason: String) -> void:
	_end_match("Disconnected: %s" % reason if reason != "" else "")


func _end_match(message: String) -> void:
	if game:
		game.leave()
		game.queue_free()
		game = null
	if not _hosting:
		TfpsLocalServices.stop_all()
	menu.in_match = false
	menu.visible = true
	_show_backdrop(settings.last_map)
	menu.open("lobby" if lobby.in_lobby() else "main")
	if message != "":
		menu.show_error(message)


func _notification(what: int) -> void:
	if what == NOTIFICATION_WM_CLOSE_REQUEST or what == NOTIFICATION_PREDELETE:
		if lobby:
			lobby.leave()
		TfpsLocalServices.stop_all()


func _test_input() -> void:
	if game:
		game.player.yaw = float(args.get("yaw", str(game.player.yaw)))
		game.player.pitch = float(args.get("pitch", "0"))
		if args.has("fire"):
			Input.action_press("fire")
		if args.has("walk"):
			Input.action_press("forward")
		if args.has("aim"):
			Input.action_press("aim")
		if args.has("reload"):
			# Fire a burst, then reload: the shot lands mid-reload.
			var t := Time.get_ticks_msec() / 1000.0
			var lead := float(args["reload"]) if str(args["reload"]).is_valid_float() else 2.4
			if t < _shot_at - lead:
				Input.action_press("fire")
			else:
				Input.action_release("fire")
				Input.action_press("reload")
		if args.has("slot"):
			game.player.slot = int(args["slot"])
	if Time.get_ticks_msec() / 1000.0 >= _shot_at:
		get_viewport().get_texture().get_image().save_png(args["shot"])
		if game:
			var wd: Dictionary = game.hud.state.get("weapon", {})
			print("weapon reloading=", wd.get("reloading"), " progress=", wd.get("reload_progress"), " ammo=", wd.get("ammo"))
		print("saved ", args["shot"], " phase=", game.hud.state.get("phase") if game else "menu",
			" players=", game.operators.size() if game else 0)
		_shot_at = -1.0
		TfpsLocalServices.stop_all()
		get_tree().quit()
