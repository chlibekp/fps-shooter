## Menus: main, practice, join by code, direct connect, lobby, settings,
## controls and pause. Built from Godot UI controls with a custom theme.
class_name TfpsMenu
extends Control

signal practice(map_name: String, bots: int, skill: float)
signal host_lobby
signal join_lobby(code: String)
signal direct_connect(address: String, map_name: String)
signal resume
signal leave_match
signal settings_changed
signal quit_game

const ACCENT := Color(0.95, 0.66, 0.18)
const MAPS := ["harbor", "villa", "consulate", "test_arena"]

var settings: TfpsSettings
var lobby: TfpsLobbyClient
var in_match := false
var _stack: Array[String] = []
var _root: VBoxContainer
var _title: Label
var _hint: Label
var _side: Control
var _rebinding := ""


func _ready() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	theme = _make_theme()
	var shade := ColorRect.new()
	shade.color = Color(0, 0, 0, 0.35)
	shade.set_anchors_preset(Control.PRESET_FULL_RECT)
	shade.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(shade)
	var brand := Label.new()
	brand.text = "TACTICAL FPS"
	brand.add_theme_font_size_override("font_size", 54)
	brand.position = Vector2(80, 70)
	add_child(brand)
	var bar := ColorRect.new()
	bar.color = ACCENT
	bar.position = Vector2(64, 82)
	bar.size = Vector2(6, 52)
	add_child(bar)
	var panel := PanelContainer.new()
	panel.position = Vector2(80, 190)
	panel.custom_minimum_size = Vector2(520, 0)
	add_child(panel)
	var inner := VBoxContainer.new()
	inner.add_theme_constant_override("separation", 10)
	panel.add_child(inner)
	_title = Label.new()
	_title.add_theme_font_size_override("font_size", 26)
	_title.add_theme_color_override("font_color", ACCENT)
	inner.add_child(_title)
	_root = VBoxContainer.new()
	_root.add_theme_constant_override("separation", 8)
	inner.add_child(_root)
	_hint = Label.new()
	_hint.add_theme_color_override("font_color", Color(1, 0.55, 0.45))
	_hint.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_hint.custom_minimum_size = Vector2(500, 0)
	inner.add_child(_hint)
	_side = PanelContainer.new()
	_side.position = Vector2(660, 190)
	_side.custom_minimum_size = Vector2(620, 0)
	_side.visible = false
	add_child(_side)
	if lobby:
		lobby.changed.connect(func() -> void:
			if current() == "lobby":
				_show("lobby"))
		lobby.failed.connect(func(m: String) -> void: _hint.text = m)
	open("main")


func _make_theme() -> Theme:
	var t := Theme.new()
	t.default_font_size = 20
	var panel := StyleBoxFlat.new()
	panel.bg_color = Color(0.05, 0.06, 0.07, 0.86)
	panel.border_color = ACCENT
	panel.border_width_top = 3
	panel.content_margin_left = 24
	panel.content_margin_right = 24
	panel.content_margin_top = 20
	panel.content_margin_bottom = 20
	t.set_stylebox("panel", "PanelContainer", panel)
	var normal := StyleBoxFlat.new()
	normal.bg_color = Color(1, 1, 1, 0.04)
	normal.content_margin_left = 16
	normal.content_margin_top = 10
	normal.content_margin_bottom = 10
	var hover := normal.duplicate()
	hover.bg_color = Color(1, 1, 1, 0.1)
	hover.border_color = ACCENT
	hover.border_width_left = 4
	var pressed := hover.duplicate()
	pressed.bg_color = Color(ACCENT.r, ACCENT.g, ACCENT.b, 0.25)
	var disabled := normal.duplicate()
	disabled.bg_color = Color(1, 1, 1, 0.02)
	for cls in ["Button", "OptionButton"]:
		t.set_stylebox("normal", cls, normal)
		t.set_stylebox("hover", cls, hover)
		t.set_stylebox("pressed", cls, pressed)
		t.set_stylebox("focus", cls, hover)
		t.set_stylebox("disabled", cls, disabled)
		t.set_color("font_color", cls, Color(0.9, 0.91, 0.89))
		t.set_color("font_hover_color", cls, Color(1, 1, 1))
		t.set_color("font_disabled_color", cls, Color(0.5, 0.5, 0.5))
	t.set_constant("h_separation", "Button", 12)
	var field := StyleBoxFlat.new()
	field.bg_color = Color(0, 0, 0, 0.45)
	field.content_margin_left = 12
	field.content_margin_top = 8
	field.content_margin_bottom = 8
	t.set_stylebox("normal", "LineEdit", field)
	var focus := field.duplicate()
	focus.border_color = ACCENT
	focus.border_width_bottom = 2
	t.set_stylebox("focus", "LineEdit", focus)
	t.set_color("font_color", "Label", Color(0.9, 0.91, 0.89))
	var grabber := StyleBoxFlat.new()
	grabber.bg_color = ACCENT
	t.set_stylebox("grabber_area", "HSlider", grabber)
	t.set_stylebox("grabber_area_highlight", "HSlider", grabber)
	var track := StyleBoxFlat.new()
	track.bg_color = Color(1, 1, 1, 0.15)
	track.content_margin_top = 3
	track.content_margin_bottom = 3
	t.set_stylebox("slider", "HSlider", track)
	return t


func current() -> String:
	return _stack[-1] if not _stack.is_empty() else ""


func open(screen: String) -> void:
	_stack = [screen]
	_show(screen)


func push(screen: String) -> void:
	_stack.append(screen)
	_show(screen)


func back() -> void:
	if _rebinding != "":
		_rebinding = ""
		_show(current())
		return
	if _stack.size() > 1:
		_stack.pop_back()
		_show(current())
	elif in_match and current() == "pause":
		resume.emit()


func _clear() -> void:
	for c in _root.get_children():
		c.queue_free()
	for c in _side.get_children():
		c.queue_free()
	_side.visible = false
	_hint.text = ""


func _button(text: String, on: Callable, enabled := true) -> Button:
	var b := Button.new()
	b.text = text
	b.alignment = HORIZONTAL_ALIGNMENT_LEFT
	b.disabled = not enabled
	b.pressed.connect(on)
	_root.add_child(b)
	return b


func _row(label: String, control: Control, parent: Control = null) -> void:
	var h := HBoxContainer.new()
	var l := Label.new()
	l.text = label
	l.custom_minimum_size = Vector2(220, 0)
	h.add_child(l)
	control.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	h.add_child(control)
	(parent if parent else _root).add_child(h)


func _choice(items: Array, selected: String, on: Callable, enabled := true) -> OptionButton:
	var o := OptionButton.new()
	for i in items.size():
		o.add_item(String(items[i]))
		if String(items[i]) == selected:
			o.select(i)
	o.disabled = not enabled
	o.item_selected.connect(func(i: int) -> void: on.call(items[i]))
	return o


func _slider(lo: float, hi: float, step: float, value: float, on: Callable) -> HSlider:
	var s := HSlider.new()
	s.min_value = lo
	s.max_value = hi
	s.step = step
	s.value = value
	s.custom_minimum_size = Vector2(220, 24)
	s.value_changed.connect(on)
	return s


func _show(screen: String) -> void:
	_clear()
	match screen:
		"main":
			_title.text = "PLAY"
			_button("Practice  (local match against bots)", func() -> void: push("practice"), TfpsLocalServices.available())
			_button("Host a lobby", func() -> void: host_lobby.emit())
			_button("Join with invite code", func() -> void: push("join"))
			_button("Direct connect", func() -> void: push("direct"))
			_button("Settings", func() -> void: push("settings"))
			_button("Quit", func() -> void: quit_game.emit())
		"practice":
			_title.text = "PRACTICE"
			var map_pick := _choice(MAPS, settings.last_map, func(m: String) -> void: settings.last_map = m)
			_row("Map", map_pick)
			var bots := SpinBox.new()
			bots.min_value = 0
			bots.max_value = 9
			bots.value = 4
			_row("Bots", bots)
			var levels := {"Easy": 0.2, "Normal": 0.5, "Hard": 0.85}
			var level := ["Normal"]
			_row("Difficulty", _choice(levels.keys(), "Normal", func(v: String) -> void: level[0] = v))
			_button("Start", func() -> void:
				settings.save()
				practice.emit(settings.last_map, int(bots.value), float(levels[level[0]])))
			_button("Back", back)
		"join":
			_title.text = "JOIN A LOBBY"
			var code := LineEdit.new()
			code.placeholder_text = "INVITE CODE"
			code.max_length = 6
			code.text_changed.connect(func(t: String) -> void:
				var up := t.to_upper()
				if up != t:
					code.text = up
					code.caret_column = up.length())
			_row("Invite code", code)
			var host := LineEdit.new()
			host.text = settings.lobby_url
			host.text_changed.connect(func(t: String) -> void: settings.lobby_url = t)
			_row("Lobby service", host)
			_button("Join", func() -> void:
				settings.save()
				join_lobby.emit(code.text))
			_button("Back", back)
			code.grab_focus.call_deferred()
		"direct":
			_title.text = "DIRECT CONNECT"
			var addr := LineEdit.new()
			addr.text = settings.last_server
			_row("Server (host:port)", addr)
			_row("Map", _choice(MAPS, settings.last_map, func(m: String) -> void: settings.last_map = m))
			_button("Connect", func() -> void:
				settings.last_server = addr.text.strip_edges()
				settings.save()
				direct_connect.emit(settings.last_server, settings.last_map))
			_button("Back", back)
		"lobby":
			_show_lobby()
		"settings":
			_show_settings()
		"controls":
			_show_controls()
		"pause":
			_title.text = "PAUSED"
			_button("Resume", func() -> void: resume.emit())
			_button("Settings", func() -> void: push("settings"))
			_button("Leave match", func() -> void: leave_match.emit())
			_button("Quit game", func() -> void: quit_game.emit())


func _show_lobby() -> void:
	var l := lobby.lobby
	var host := lobby.is_host()
	var s: Dictionary = l.get("settings", {})
	_title.text = "LOBBY"
	var me := {}
	for m in l.get("members", []):
		if int(m.get("id", -1)) == int(l.get("you", -2)):
			me = m
	var squad_pick := _choice(["Squad A", "Squad B"], "Squad A" if int(me.get("squad", 0)) == 0 else "Squad B",
		func(v: String) -> void: lobby.set_squad(0 if v == "Squad A" else 1))
	_row("Squad", squad_pick)
	var ready := CheckButton.new()
	ready.button_pressed = me.get("ready", false)
	ready.toggled.connect(func(on: bool) -> void: lobby.set_ready(on))
	_row("Ready", ready)
	var maps: Array = lobby.maps if not lobby.maps.is_empty() else MAPS
	var change := func(key: String, value) -> void:
		var ns := s.duplicate()
		ns[key] = value
		lobby.set_settings(ns)
	_row("Map", _choice(maps, s.get("map", ""), func(v: String) -> void: change.call("map", v), host))
	_row("Mode", _choice(["defuse", "elimination", "deathmatch"], s.get("mode", ""), func(v: String) -> void: change.call("mode", v), host))
	_row("Teams", _choice(["1v1", "2v2", "3v3", "4v4", "5v5"], "%dv%d" % [int(s.get("team_size", 5)), int(s.get("team_size", 5))],
		func(v: String) -> void: change.call("team_size", int(v.left(1))), host))
	if s.get("mode", "") == "deathmatch":
		_row("Kill limit", _choice(["10", "25", "50", "75", "100"], str(int(s.get("score_limit", 50))), func(v: String) -> void: change.call("score_limit", int(v)), host))
	else:
		_row("Rounds to win", _choice(["1", "2", "3", "4", "5", "6"], str(int(s.get("rounds_to_win", 4))), func(v: String) -> void: change.call("rounds_to_win", int(v)), host))
	_row("Round time", _choice(["120", "180", "240", "300"], str(int(s.get("round_time", 180))), func(v: String) -> void: change.call("round_time", float(v)), host))
	var ff := CheckButton.new()
	ff.button_pressed = s.get("friendly_fire", false)
	ff.disabled = not host
	ff.toggled.connect(func(on: bool) -> void: change.call("friendly_fire", on))
	_row("Friendly fire", ff)
	_row("Server tick rate", _choice(["64", "128"], str(int(s.get("tick_rate", 64))), func(v: String) -> void: change.call("tick_rate", int(v)), host))
	_button("Start match", func() -> void: lobby.start_match(), host)
	_button("Leave lobby", func() -> void:
		lobby.leave()
		open("main"))
	# Side panel: invite code and squads.
	_side.visible = true
	var v := VBoxContainer.new()
	v.add_theme_constant_override("separation", 6)
	_side.add_child(v)
	var cap := Label.new()
	cap.text = "INVITE CODE"
	cap.add_theme_color_override("font_color", Color(0.6, 0.62, 0.6))
	v.add_child(cap)
	var c := Label.new()
	c.text = lobby.code()
	c.add_theme_font_size_override("font_size", 64)
	c.add_theme_color_override("font_color", ACCENT)
	v.add_child(c)
	var h := HBoxContainer.new()
	h.add_theme_constant_override("separation", 40)
	v.add_child(h)
	for sq in 2:
		var col := VBoxContainer.new()
		col.custom_minimum_size = Vector2(260, 0)
		h.add_child(col)
		var head := Label.new()
		head.text = "SQUAD A" if sq == 0 else "SQUAD B"
		head.add_theme_color_override("font_color", Color(0.35, 0.7, 1.0) if sq == 0 else Color(1.0, 0.45, 0.3))
		col.add_child(head)
		for m in l.get("members", []):
			if int(m.get("squad", 0)) != sq:
				continue
			var row := Label.new()
			var tag := "  HOST" if int(m.get("id", -1)) == int(l.get("host", -2)) else ("  READY" if m.get("ready", false) else "")
			row.text = String(m.get("name", "")) + tag
			col.add_child(row)
	var status := Label.new()
	status.text = "Match running." if l.get("status", "") == "in_game" else ("Start when everyone is ready." if host else "Waiting for the host.")
	status.add_theme_color_override("font_color", Color(0.6, 0.62, 0.6))
	v.add_child(status)


func _show_settings() -> void:
	_title.text = "SETTINGS"
	var name := LineEdit.new()
	name.text = settings.player_name
	name.max_length = 16
	name.text_changed.connect(func(t: String) -> void: settings.player_name = t)
	_row("Name", name)
	_row("Mouse sensitivity", _slider(0.01, 0.3, 0.005, settings.sensitivity, func(v: float) -> void: settings.sensitivity = v))
	_row("Aiming sensitivity", _slider(0.2, 1.5, 0.05, settings.ads_sensitivity, func(v: float) -> void: settings.ads_sensitivity = v))
	var inv := CheckButton.new()
	inv.button_pressed = settings.invert_y
	inv.toggled.connect(func(on: bool) -> void: settings.invert_y = on)
	_row("Invert mouse Y", inv)
	var tc := CheckButton.new()
	tc.button_pressed = settings.toggle_crouch
	tc.toggled.connect(func(on: bool) -> void: settings.toggle_crouch = on)
	_row("Crouch key toggles", tc)
	_row("Field of view", _slider(70, 110, 1, settings.fov, func(v: float) -> void: settings.fov = v))
	_row("Master volume", _slider(0, 1, 0.05, settings.master_volume, func(v: float) -> void: settings.master_volume = v))
	_row("Effects volume", _slider(0, 1, 0.05, settings.effects_volume, func(v: float) -> void: settings.effects_volume = v))
	_row("Graphics", _choice(["Low", "Medium", "High", "Ultra"], ["Low", "Medium", "High", "Ultra"][settings.quality],
		func(v: String) -> void: settings.quality = ["Low", "Medium", "High", "Ultra"].find(v)))
	# Armour class: index 0 medium, 1 light, 2 heavy (as the server expects).
	var armours := ["Medium (100 HP)", "Light (90 HP, +10% speed)", "Heavy (120 HP, -10% speed)"]
	_row("Armour", _choice(armours, armours[settings.armor], func(v: String) -> void: settings.armor = armours.find(v)))
	var lob := LineEdit.new()
	lob.text = settings.lobby_url
	lob.text_changed.connect(func(t: String) -> void: settings.lobby_url = t)
	_row("Lobby service", lob)
	_button("Controls", func() -> void: push("controls"))
	_button("Save and back", func() -> void:
		settings.save()
		settings_changed.emit()
		back())


func _show_controls() -> void:
	_title.text = "CONTROLS"
	var scroll := ScrollContainer.new()
	scroll.custom_minimum_size = Vector2(500, 520)
	_root.add_child(scroll)
	var list := VBoxContainer.new()
	list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(list)
	for action in TfpsControls.DEFAULTS:
		if action == "menu":
			continue
		var b := Button.new()
		b.text = "press a key..." if _rebinding == action else TfpsControls.describe(action)
		b.pressed.connect(func() -> void:
			_rebinding = action
			_show("controls"))
		_row(action.capitalize(), b, list)
	_button("Reset to defaults", func() -> void:
		settings.keys = {}
		TfpsControls.install()
		settings.save()
		_show("controls"))
	_button("Back", back)


func _input(event: InputEvent) -> void:
	if _rebinding == "" or not visible:
		return
	var code := -1
	if event is InputEventKey and event.pressed:
		code = event.physical_keycode
		if code == KEY_ESCAPE:
			back()
			get_viewport().set_input_as_handled()
			return
	elif event is InputEventMouseButton and event.pressed:
		code = event.button_index
	if code < 0:
		return
	# A key does one thing: whatever used it takes this action's old key.
	var old: Array = settings.keys.get(_rebinding, TfpsControls.DEFAULTS[_rebinding])
	for a in TfpsControls.DEFAULTS:
		var cur: Array = settings.keys.get(a, TfpsControls.DEFAULTS[a])
		if a != _rebinding and cur.has(code):
			settings.keys[a] = old
	settings.keys[_rebinding] = [code]
	TfpsControls.install(settings.keys)
	settings.save()
	_rebinding = ""
	get_viewport().set_input_as_handled()
	_show("controls")


func show_error(message: String) -> void:
	_hint.text = message
