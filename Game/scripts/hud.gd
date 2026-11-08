## In-game HUD: crosshair, health, ammo and gadgets, match bar, kill feed,
## hit markers, damage direction, flash whiteout, messages and net stats.
class_name TfpsHud
extends Control

const ACCENT := Color(0.95, 0.66, 0.18)
const TEXT := Color(0.93, 0.94, 0.92)
const DIM := Color(0.62, 0.65, 0.63)
const PANEL := Color(0.03, 0.035, 0.04, 0.62)

var state: Dictionary = {}
var my_squad := 0
var show_net := false
var status_text := ""
var kill_feed: Array = []       # [{text, time, mine}]
var hit_time := -10.0
var hit_kill := false
var damage_time := -10.0
var damage_from := Vector3.ZERO
var flash_time := -10.0
var flash_strength := 0.0
var flash_hold := 3.0
var center_message := ""
var camera: Camera3D
var _font: Font


func _ready() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	_font = ThemeDB.fallback_font


func _now() -> float:
	return Time.get_ticks_msec() / 1000.0


func _process(_delta: float) -> void:
	queue_redraw()


func _text(t: String, pos: Vector2, size: int, color: Color, align := HORIZONTAL_ALIGNMENT_LEFT, width := -1.0) -> void:
	draw_string_outline(_font, pos, t, align, width, size, 4, Color(0, 0, 0, 0.6))
	draw_string(_font, pos, t, align, width, size, color)


func _panel(r: Rect2) -> void:
	draw_rect(r, PANEL)
	draw_rect(Rect2(r.position, Vector2(3, r.size.y)), ACCENT)


func _draw() -> void:
	var vs := get_viewport_rect().size
	var s := vs.y / 1080.0
	var c := vs * 0.5
	var now := _now()
	if state.is_empty() or state.get("phase", "offline") != "connected":
		_text(status_text, Vector2(40, 60) * s, int(22 * s), TEXT)
		return
	var alive: bool = state.get("alive", true)
	# Crosshair: opens with movement, closes when aiming; hidden when aiming down sights.
	var aim := float(state.get("aim", 0.0))
	if alive and aim < 0.6:
		var vel := TfpsNet.vec(state.get("velocity", [0, 0, 0]))
		var gap := (6.0 + Vector2(vel.x, vel.z).length() * 3.0 + (12.0 if not state.get("on_ground", true) else 0.0)) * s * (1.0 - aim)
		var len := 9.0 * s
		var col := Color(1, 1, 1, 0.9 * (1.0 - aim))
		for d in [Vector2(1, 0), Vector2(-1, 0), Vector2(0, 1), Vector2(0, -1)]:
			draw_line(c + d * gap, c + d * (gap + len), Color(0, 0, 0, 0.5 * col.a), 4.0 * s)
			draw_line(c + d * gap, c + d * (gap + len), col, 2.0 * s)
		draw_circle(c, 1.5 * s, col)
	# Hit marker.
	var ht := now - hit_time
	if ht < 0.25:
		var a := 1.0 - ht / 0.25
		var hc := (Color(1, 0.25, 0.2, a) if hit_kill else Color(1, 1, 1, a))
		for d in [Vector2(1, 1), Vector2(-1, 1), Vector2(1, -1), Vector2(-1, -1)]:
			draw_line(c + d * 8.0 * s, c + d * 16.0 * s, hc, 2.5 * s)
	# Damage direction.
	var dt := now - damage_time
	if dt < 1.2 and camera:
		var to := damage_from - camera.global_position
		var fwd := -camera.global_basis.z
		var right := camera.global_basis.x
		var ang := atan2(to.dot(right), to.dot(fwd))
		var dir := Vector2(sin(ang), -cos(ang))
		var p := c + dir * 140.0 * s
		var a2 := 1.0 - dt / 1.2
		draw_colored_polygon(PackedVector2Array([p + dir * 18.0 * s, p + dir.orthogonal() * 22.0 * s, p - dir.orthogonal() * 22.0 * s]), Color(0.9, 0.15, 0.1, 0.8 * a2))
	# Health (bottom left).
	var hp := int(state.get("health", 100))
	_panel(Rect2(Vector2(30, vs.y / s - 110) * s, Vector2(300, 80) * s))
	_text(str(hp), Vector2(46, vs.y / s - 52) * s, int(44 * s), TEXT if hp > 35 else Color(1, 0.35, 0.3))
	draw_rect(Rect2(Vector2(130, vs.y / s - 78) * s, Vector2(180, 10) * s), Color(1, 1, 1, 0.15))
	draw_rect(Rect2(Vector2(130, vs.y / s - 78) * s, Vector2(180 * clampf(hp / 100.0, 0, 1), 10) * s), TEXT if hp > 35 else Color(1, 0.35, 0.3))
	_text("HEALTH", Vector2(130, vs.y / s - 88) * s, int(13 * s), DIM)
	# Weapon and ammo (bottom right).
	var w: Dictionary = state.get("weapon", {})
	var gadgets: Array = state.get("gadgets", [])
	var bx := vs.x / s - 330
	_panel(Rect2(Vector2(bx, vs.y / s - 150) * s, Vector2(300, 120) * s))
	if int(w.get("active", 0)) < 2 and w.has("ammo"):
		_text("%d" % int(w["ammo"]), Vector2(bx + 18, vs.y / s - 70) * s, int(48 * s), TEXT if int(w["ammo"]) > 0 else Color(1, 0.35, 0.3))
		_text("/ %d" % int(w.get("reserve", 0)), Vector2(bx + 110, vs.y / s - 72) * s, int(22 * s), DIM)
		_text(String(w.get("name", "")).to_upper(), Vector2(bx + 18, vs.y / s - 122) * s, int(16 * s), ACCENT)
		if w.get("reloading", false):
			var rp := float(w.get("reload_progress", 0.0))
			draw_rect(Rect2(Vector2(bx + 18, vs.y / s - 52) * s, Vector2(260 * rp, 4) * s), ACCENT)
	var gx := bx + 18.0
	for i in gadgets.size():
		var g: Dictionary = gadgets[i]
		if int(g.get("id", 0)) == 0:
			continue
		var active := int(w.get("active", 0)) == i + 2
		_text("%d %s x%d" % [i + 3, String(g.get("name", "")).get_slice(" ", 0).left(7), int(g.get("count", 0))], Vector2(gx, vs.y / s - 38) * s, int(12 * s), ACCENT if active else DIM)
		gx += 70.0
	# Match bar (top centre).
	var m: Dictionary = state.get("match", {})
	if not m.is_empty():
		var score: Array = m.get("score", [0, 0])
		var mine := int(score[my_squad]) if score.size() == 2 else 0
		var theirs := int(score[1 - my_squad]) if score.size() == 2 else 0
		var t := maxf(0.0, float(m.get("time_left", 0.0)))
		var phase_names := ["WARMUP", "PREPARATION", "ACTION", "ROUND OVER", "MATCH OVER"]
		var phase := int(m.get("phase", 0))
		_panel(Rect2(Vector2(c.x / s - 170, 18) * s, Vector2(340, 76) * s))
		_text("%d" % mine, Vector2(c.x / s - 150, 66) * s, int(36 * s), Color(0.35, 0.7, 1.0))
		_text("%d" % theirs, Vector2(c.x / s + 115, 66) * s, int(36 * s), Color(1.0, 0.45, 0.3))
		_text("--:--" if int(m.get("mode", 0)) == 0 and t <= 0.0 else "%d:%02d" % [int(t) / 60, int(t) % 60], Vector2(c.x / s - 50, 62) * s, int(32 * s), TEXT, HORIZONTAL_ALIGNMENT_CENTER, 100 * s)
		var side := "ATTACK" if int(state.get("side", 0)) == 0 else "DEFEND"
		var line: String
		if int(m.get("mode", 0)) == 0:
			line = "TEAM DEATHMATCH  ·  %s" % ("MATCH OVER" if phase == 4 else "FIRST TO THE KILL LIMIT")
		else:
			line = "%s  ·  ROUND %d  ·  %s" % [phase_names[clampi(phase, 0, 4)], int(m.get("round", 0)), side]
		_text(line, Vector2(c.x / s - 170, 88) * s, int(13 * s), DIM, HORIZONTAL_ALIGNMENT_CENTER, 340 * s)
	# Kill feed (top right).
	var ky := 40.0
	for e in kill_feed:
		if now - float(e["time"]) > 6.0:
			continue
		_text(e["text"], Vector2(vs.x / s - 520, ky) * s, int(16 * s), ACCENT if e["mine"] else TEXT, HORIZONTAL_ALIGNMENT_RIGHT, 490 * s)
		ky += 24.0
	# Hold action (plant / defuse / reinforce).
	var action := int(state.get("action", 0))
	if action > 0:
		var label: String = ["", "REINFORCING", "PLANTING", "DEFUSING"][clampi(action, 0, 3)]
		var prog := float(state.get("reinforce_progress", 0.0))
		_text(label, Vector2(c.x / s - 150, c.y / s + 90) * s, int(16 * s), ACCENT, HORIZONTAL_ALIGNMENT_CENTER, 300 * s)
		draw_rect(Rect2(Vector2(c.x / s - 150, c.y / s + 100) * s, Vector2(300, 6) * s), Color(1, 1, 1, 0.2))
		draw_rect(Rect2(Vector2(c.x / s - 150, c.y / s + 100) * s, Vector2(300 * prog, 6) * s), ACCENT)
	# Death screen.
	if not alive:
		draw_rect(Rect2(Vector2.ZERO, vs), Color(0.15, 0.0, 0.0, 0.25))
		var ri := float(state.get("respawn_in", 0.0))
		_text("YOU ARE DOWN" if ri <= 0.0 else "RESPAWN IN %.1f" % ri, Vector2(0, c.y / s - 120) * s, int(30 * s), TEXT, HORIZONTAL_ALIGNMENT_CENTER, vs.x)
	if center_message != "":
		_text(center_message, Vector2(0, c.y / s - 200) * s, int(28 * s), ACCENT, HORIZONTAL_ALIGNMENT_CENTER, vs.x)
	# Network stats (F3).
	if show_net:
		var st: Dictionary = state.get("stats", {})
		_text("ping %.0f ms   tick %d Hz   corrections %d   in %.0f kB   out %.0f kB" % [float(st.get("rtt_ms", 0)), int(st.get("tick_rate", 0)),
			int(st.get("corrections", 0)), float(st.get("bytes_in", 0)) / 1024.0, float(st.get("bytes_out", 0)) / 1024.0], Vector2(30, 30) * s, int(14 * s), DIM)
	# Flashbang whiteout (drawn last, over everything).
	var ft := now - flash_time
	if ft < flash_hold + 1.5:
		var a3 := flash_strength * (1.0 if ft < flash_hold else 1.0 - (ft - flash_hold) / 1.5)
		draw_rect(Rect2(Vector2.ZERO, vs), Color(1, 1, 0.98, clampf(a3, 0.0, 1.0)))
