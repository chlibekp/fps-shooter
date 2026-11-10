## A match: owns the native client, world, local player, other players,
## gadgets, effects, audio and HUD, and moves the frame state between them.
class_name TfpsGame
extends Node3D

signal left_match(reason: String)

var net: TfpsNet
var world: TfpsWorld
var player: TfpsPlayer
var effects: TfpsEffects
var audio: TfpsAudio
var hud: TfpsHud
var operators := {}          # player id -> TfpsOperator
var gadget_nodes := {}       # gadget entity id -> Node3D
var names := {}              # player id -> name
var drone_hums := {}         # gadget entity id -> AudioStreamPlayer3D
var paused := false
var _step_travel := 0.0
var _last_feet := Vector3.ZERO
var _remote_travel := {}
var _was_ground := true
var _was_reloading := false
var _last_phase := -1


func start(p_net: TfpsNet, address: String, player_name: String, join_token := "0", armor := 0, attachments := 0, primary := 0) -> bool:
	net = p_net
	world = TfpsWorld.new()
	add_child(world)
	world.build(net.world, net)
	effects = TfpsEffects.new()
	add_child(effects)
	audio = TfpsAudio.new()
	add_child(audio)
	audio.setup(net)
	player = TfpsPlayer.new()
	player.attachments = attachments
	add_child(player)
	var layer := CanvasLayer.new()
	add_child(layer)
	hud = TfpsHud.new()
	layer.add_child(hud)
	hud.camera = player.camera
	# Start looking the way our team's first spawn faces until the server places us.
	var spawns: Array = net.world.get("spawns", [])
	if not spawns.is_empty():
		player.yaw = float(spawns[0]["yaw"])
	hud.status_text = "Connecting to %s..." % address
	return net.connect_to(address, player_name, join_token, armor, attachments, primary)


func _process(delta: float) -> void:
	if net == null:
		return
	player.input_enabled = not paused
	var slot := player.slot
	var device := _my_remote_device()
	if not device.is_empty():
		slot |= TfpsNet.REMOTE_CONTROL
	var s := net.frame(player.buttons(), player.yaw, player.pitch, slot)
	hud.state = s
	var phase: String = s.get("phase", "offline")
	if phase == "connecting":
		return
	if phase != "connected":
		var reason: String = s.get("reason", "disconnected")
		hud.status_text = "Disconnected: %s" % reason
		set_process(false)
		left_match.emit(reason)
		return
	hud.my_squad = int(s.get("squad", 0))
	# How far the view is from a wall ahead (the view model tucks in).
	var cam := player.camera.global_transform
	var fwd := -cam.basis.z
	var probe := net.request({"op": "raycast", "origin": [cam.origin.x, cam.origin.y, cam.origin.z],
		"direction": [fwd.x, fwd.y, fwd.z], "max": 1.2})
	player.wall_distance = float(probe.get("distance", 1.2)) if probe.get("hit", false) else 1.2
	player.apply(s, delta, effects.shake)
	# Viewing through a drone or wall camera of ours: the camera moves to it.
	device = _my_remote_device()
	hud.remote_view = ""
	if not device.is_empty():
		var at := TfpsNet.vec(device["position"])
		var drone := int(device.get("kind", -1)) == 4
		player.camera.global_position = at + Vector3(0, 0.12 if drone else 0.0, 0)
		player.camera.fov = 100.0 if drone else 95.0
		hud.remote_view = "DRONE" if drone else "CAMERA"
	hud.objectives = net.world.get("objectives", [])
	if audio:
		audio.position = Vector3.ZERO
	_update_players(s.get("players", []), delta)
	_update_gadgets(s.get("gadget_entities", []))
	# Our own drone / camera is not drawn while we look through it.
	if not device.is_empty() and gadget_nodes.has(int(device["id"])):
		gadget_nodes[int(device["id"])].visible = false
	_handle_shots(s.get("shots", []))
	_handle_events(s.get("events", []))
	var destroyed: Array = s.get("destroyed", [])
	if not destroyed.is_empty():
		var broken := world.destroy_cells(destroyed)
		var per_material := {}
		for b in broken:
			effects.breakage(b["center"], b["size"], b["material"])
			per_material[b["material"]] = b["center"]
		for mat in per_material:
			var cue := "glass_break" if mat == 5 else ("wood_break" if mat == 2 else "wall_break")
			audio.play_at(audio.cue(cue), per_material[mat])
	var added: Array = s.get("added_solids", [])
	if not added.is_empty():
		var w := net.request({"op": "world"})
		world.add_runtime_solids(w, added)
		for i in added:
			audio.play_at(audio.cue("barricade_place"), player.camera.global_position)
	for dc in s.get("doors", []):
		world.set_door(int(dc["index"]), bool(dc["open"]))
		var db: AABB = world.doors[int(dc["index"])]["box"] if world.doors.has(int(dc["index"])) else AABB()
		audio.play_at(audio.cue("door_open" if dc["open"] else "door_close"), db.get_center())
	# Interaction hint: the door in front of the crosshair, within reach.
	hud.prompt = ""
	if s.get("alive", false):
		var door := world.door_in_view(player.camera.global_position, -player.camera.global_basis.z, 2.0)
		if door >= 0:
			var verb := "CLOSE" if world.doors[door]["open"] else "OPEN"
			hud.prompt = "%s  %s DOOR" % [TfpsControls.describe("use"), verb]
	var reinforced: Array = s.get("reinforced_solids", [])
	if not reinforced.is_empty():
		world.reinforce(reinforced)
		audio.play_2d(audio.cue("reinforce"), 0.7)
	if s.get("map_reset", false):
		world.build(net.request({"op": "world"}), net)
	_local_sounds(s)


func _update_players(list: Array, delta: float) -> void:
	var seen := {}
	for p in list:
		var id := int(p["id"])
		seen[id] = true
		names[id] = p.get("name", "")
		var op: TfpsOperator = operators.get(id)
		if op == null:
			op = TfpsOperator.new()
			op.player_id = id
			add_child(op)
			operators[id] = op
			_remote_travel[id] = [TfpsNet.vec(p["position"]), 0.0]
		op.apply(p, delta)
		# Footsteps: one per stride, louder when sprinting, quiet when crouched.
		var flags := int(p["flags"])
		var pos := TfpsNet.vec(p["position"])
		var tr: Array = _remote_travel[id]
		var moved := Vector2(pos.x - tr[0].x, pos.z - tr[0].z).length()
		tr[0] = pos
		if moved < 1.0 and flags & TfpsNet.F_ON_GROUND and flags & TfpsNet.F_ALIVE:
			tr[1] += moved
			var stride := 2.2 if flags & TfpsNet.F_SPRINTING else 1.7
			if tr[1] >= stride:
				tr[1] = 0.0
				var quiet := flags & (TfpsNet.F_CROUCHED | TfpsNet.F_PRONE)
				audio.play_at(audio.step_cue(_ground_material(pos)), pos, 0.35 if quiet else (1.0 if flags & TfpsNet.F_SPRINTING else 0.7))
	for id in operators.keys():
		if not seen.has(id):
			operators[id].queue_free()
			operators.erase(id)
			_remote_travel.erase(id)


## A deployed drone / wall camera of ours that the selected gadget slot controls.
func _my_remote_device() -> Dictionary:
	if player.slot < 2 or hud.state.is_empty():
		return {}
	var stock: Array = hud.state.get("gadgets", [])
	var idx := player.slot - 2
	if idx >= stock.size():
		return {}
	var gid := int(stock[idx].get("id", 0))
	var me := int(hud.state.get("player_id", -1))
	for g in hud.state.get("gadget_entities", []):
		var kind := int(g.get("kind", -1))
		if int(g.get("owner", -2)) == me and int(g.get("gadget", -1)) == gid and (kind == 4 or kind == 5):
			return g
	return {}


func _ground_material(at: Vector3) -> int:
	var hit := net.request({"op": "raycast", "origin": [at.x, at.y + 0.1, at.z], "direction": [0, -1, 0], "max": 0.6})
	return int(hit.get("material", 0)) if hit.get("hit", false) else 0


func _update_gadgets(list: Array) -> void:
	var seen := {}
	for g in list:
		var id := int(g["id"])
		seen[id] = true
		var pos := TfpsNet.vec(g["position"])
		var kind := int(g.get("kind", -1))
		var node: Node3D = gadget_nodes.get(id)
		if node == null:
			node = _gadget_model(kind)
			add_child(node)
			gadget_nodes[id] = node
			node.position = pos
		node.position = node.position.lerp(pos, 0.5)
		node.visible = true
		if int(g["phase"]) == 4:  # smoking
			effects.smoke_cloud(id, pos, float(g.get("radius", 4.0)))
			node.visible = false
		if kind == 4 and not drone_hums.has(id):  # drone motor
			var hum := AudioStreamPlayer3D.new()
			node.add_child(hum)
			hum.stream = audio._stream(audio.cue("drone_hum"), 0)
			hum.max_distance = 20.0
			hum.play()
			drone_hums[id] = hum
	for id in gadget_nodes.keys():
		if not seen.has(id):
			gadget_nodes[id].queue_free()
			gadget_nodes.erase(id)
			drone_hums.erase(id)
			effects.stop_smoke(id)


func _gadget_model(kind: int) -> Node3D:
	# The Blender models when they were imported (Game/assets), else primitives.
	var asset: String = {0: "FragGrenade", 1: "FragGrenade", 2: "FragGrenade", 3: "BreachCharge", 4: "ReconDrone", 5: "WallCamera"}.get(kind, "")
	if asset != "":
		var model := TfpsProps.instance(asset)
		if model != null:
			var holder := Node3D.new()
			holder.add_child(model)
			return holder
	var n := Node3D.new()
	var dark := TfpsModels.mat("gadget_dark", Color(0.12, 0.13, 0.12), 0.5, 0.4)
	var olive := TfpsModels.mat("gadget_olive", Color(0.3, 0.33, 0.22), 0.7)
	var led := TfpsModels.mat("gadget_led", Color(1, 0.1, 0.05), 0.3, 0.0, Color(1, 0.1, 0.05))
	match kind:
		0, 1, 2:  # grenades
			TfpsModels.sphere(n, 0.035, Vector3.ZERO, olive, Vector3(1, 1.3, 1))
			TfpsModels.box(n, Vector3(0.02, 0.03, 0.02), Vector3(0, 0.05, 0), dark)
		3:  # breach charge
			TfpsModels.box(n, Vector3(0.24, 0.34, 0.06), Vector3(0, 0.17, 0), olive)
			TfpsModels.box(n, Vector3(0.03, 0.03, 0.01), Vector3(0, 0.3, -0.035), led)
		4:  # drone
			TfpsModels.box(n, Vector3(0.18, 0.06, 0.24), Vector3(0, 0.07, 0), dark)
			for x in [-0.12, 0.12]:
				TfpsModels.cyl(n, 0.07, 0.04, Vector3(x, 0.07, 0), olive)
			TfpsModels.sphere(n, 0.015, Vector3(0, 0.09, -0.12), led)
		_:  # cameras, sensors, barricade kit
			TfpsModels.box(n, Vector3(0.1, 0.1, 0.1), Vector3(0, 0.05, 0), dark)
			TfpsModels.sphere(n, 0.012, Vector3(0, 0.08, -0.05), led)
	return n


func _handle_shots(shots: Array) -> void:
	for sh in shots:
		var local: bool = sh.get("local", false)
		var origin := player.muzzle_position() if local else TfpsNet.vec(sh["origin"]) + Vector3(0, -0.1, 0)
		var end := TfpsNet.vec(sh["end"])
		var surface := int(sh.get("surface", 255))
		if not sh.get("suppressed", false):
			effects.muzzle(origin, end - origin, local)
		effects.tracer(origin, end)
		if surface != 255:
			effects.impact(end, (origin - end).normalized(), surface)
			audio.play_at(audio.impact_cue(surface), end, 0.8)
		var cue := audio.shot_cue(int(sh.get("weapon_id", 1)))
		var quiet: bool = sh.get("suppressed", false)
		if local:
			audio.play_2d(cue, 0.35 if quiet else 0.9)
			var wid := int(sh.get("weapon_id", 1))
			var b := player.camera.global_basis
			var throw := b * Vector3(randf_range(1.4, 2.2), randf_range(1.2, 1.9), randf_range(-0.2, 0.4))
			effects.shell(player.ejection_position(), throw + TfpsNet.vec(hud.state.get("velocity", [0, 0, 0])), wid == 5)
		else:
			audio.play_at(cue, origin, 0.3 if quiet else 1.0)
			_suppress(sh, origin, end)


## An enemy round passing close to the head suppresses: vignette, shake and
## a supersonic crack (only for rounds that came near, not hits on us).
func _suppress(sh: Dictionary, origin: Vector3, end: Vector3) -> void:
	var shooter = operators.get(int(sh.get("shooter", -1)))
	if shooter != null and (shooter as TfpsOperator).team == int(hud.state.get("side", -1)):
		return
	var head := player.camera.global_position
	var seg := end - origin
	if seg.length() < 0.01:
		return
	var t := clampf((head - origin).dot(seg) / seg.length_squared(), 0.0, 1.0)
	if t <= 0.0 or t >= 1.0 and origin.distance_to(end) < origin.distance_to(head):
		return   # the round stopped before reaching us
	var d := head.distance_to(origin + seg * t)
	if d > 2.0 or d < 0.05:
		return
	var k := 1.0 - d / 2.0
	hud.suppression = minf(1.0, hud.suppression + 0.35 + 0.4 * k)
	effects.shake = maxf(effects.shake, 0.25 * k)
	audio.play_2d(audio.cue("bullet_whiz"), 0.5 + 0.5 * k)


func _handle_events(events: Array) -> void:
	var now := Time.get_ticks_msec() / 1000.0
	for e in events:
		match e["type"]:
			"hit_confirm":
				hud.hit_time = now
				hud.hit_kill = e.get("killed", false)
				audio.play_2d(audio.cue("kill_confirm" if hud.hit_kill else "hit_marker"))
			"damaged":
				hud.damage_time = now
				hud.damage_from = TfpsNet.vec(e["position"])
				audio.play_2d(audio.cue("impact_flesh"), 0.8)
			"kill":
				var killer: String = names.get(int(e["other_id"]), "you" if int(e["other_id"]) == int(hud.state.get("player_id", 0)) else "?")
				var victim: String = names.get(int(e["player_id"]), "you" if int(e["player_id"]) == int(hud.state.get("player_id", 0)) else "?")
				var me := int(hud.state.get("player_id", 0))
				hud.kill_feed.append({"text": "%s  ✕  %s%s" % [killer, victim, "  (headshot)" if e.get("headshot", false) else ""],
					"time": now, "mine": int(e["other_id"]) == me or int(e["player_id"]) == me})
				if hud.kill_feed.size() > 6:
					hud.kill_feed.pop_front()
			"explosion":
				var at := TfpsNet.vec(e["position"])
				effects.explosion(at, float(e.get("radius", 4.0)))
				audio.play_at(audio.cue("explosion"), at)
			"flash":
				var at2 := TfpsNet.vec(e["position"])
				effects.flash(at2)
				audio.play_at(audio.cue("flashbang"), at2)
				var k := float(e.get("intensity", 0.0))
				if k > 0.0:
					hud.flash_time = now
					hud.flash_strength = k
					hud.flash_hold = maxf(0.5, 4.0 * k)
					if k > 0.3:
						audio.play_2d(audio.cue("tinnitus"), k)
			"match_state":
				var m: Dictionary = hud.state.get("match", {})
				var ph := int(m.get("phase", 0))
				if ph != _last_phase:
					if ph == 1:
						audio.play_2d(audio.cue("round_start"))
					elif (ph == 3 or ph == 4) and int(m.get("last_winner", 255)) != 255:
						audio.play_2d(audio.cue("round_win" if int(m["last_winner"]) == hud.my_squad else "round_lose"))
					_last_phase = ph
			"player_joined":
				names[int(e["player_id"])] = e.get("name", "")
			"spotted":
				hud.spots[int(e["player_id"])] = {"position": TfpsNet.vec(e["position"]), "until": now + float(e.get("duration", 3.0))}


func _local_sounds(s: Dictionary) -> void:
	var feet := TfpsNet.vec(s.get("feet", [0, 0, 0]))
	var on_ground: bool = s.get("on_ground", true)
	var moved := Vector2(feet.x - _last_feet.x, feet.z - _last_feet.z).length()
	_last_feet = feet
	if on_ground and moved < 1.0 and s.get("alive", true):
		_step_travel += moved
		var sprint := Input.is_action_pressed("sprint")
		if _step_travel >= (2.2 if sprint else 1.7):
			_step_travel = 0.0
			var quiet: bool = s.get("crouched", false) or s.get("prone", false) or Input.is_action_pressed("walk")
			audio.play_2d(audio.step_cue(_ground_material(feet)), 0.25 if quiet else 0.45)
	if on_ground and not _was_ground:
		audio.play_2d(audio.cue("land"), 0.6)
	_was_ground = on_ground
	var reloading: bool = s.get("weapon", {}).get("reloading", false)
	if reloading and not _was_reloading:
		audio.play_2d(audio.cue("mag_out"))
	elif _was_reloading and not reloading:
		audio.play_2d(audio.cue("mag_in"))
	_was_reloading = reloading


func leave() -> void:
	if net:
		net.disconnect_from()
	set_process(false)
