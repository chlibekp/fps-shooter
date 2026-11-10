## Another player: the operator model animated from the replicated state:
## walk cycle from speed, crouch, prone, lean, aim pitch, death, and arms that
## reach the weapon (two-bone IK). Uses the Blender body parts
## (Operator<team><part>, joints from their manifests) when they are synced,
## otherwise a figure built from primitives.
class_name TfpsOperator
extends Node3D

const TEAM_UNIFORM := [Color(0.33, 0.35, 0.24), Color(0.17, 0.2, 0.27)]
const TEAM_ACCENT := [Color(0.95, 0.55, 0.12), Color(0.2, 0.55, 0.95)]

var player_id := 0
var team := -1
var _hips: Node3D
var _torso: Node3D
var _head: Node3D
var _legs: Array[Node3D] = []   # [left thigh, left shin, right thigh, right shin]
var _weapon_pivot: Node3D
var _weapon: Node3D
var _weapon_id := -1
var _label: Label3D
var _last_pos := Vector3.ZERO
var _speed := 0.0
var _phase := 0.0
var _crouch := 0.0
var _prone := 0.0
var _dead := 0.0
var _lean := 0.0
var _pitch := 0.0
var _materials := {}
var _use_assets := false
var _arms: Array[Node3D] = []   # [right upper, right fore, left upper, left fore]

const UPPER_ARM := 0.3
const FOREARM := 0.29
const SHOULDER := Vector3(0.2, 0.52, 0.0)


func _init() -> void:
	_make_label()
	_use_assets = TfpsProps.available("OperatorATorso") and TfpsProps.available("OperatorBTorso")
	if not _use_assets:
		_build()


## A body part placed so its joint is at the returned node's origin; `mirror`
## flips it for the left side.
static func _part(asset: String, mirror := false) -> Node3D:
	var joint := Node3D.new()
	var inst := TfpsProps.instance(asset)
	if inst == null:
		return joint
	var j := Vector3.ZERO
	var f := FileAccess.open("res://assets/%s/manifest.json" % asset, FileAccess.READ)
	if f != null:
		var m: Variant = JSON.parse_string(f.get_as_text())
		if m is Dictionary and (m as Dictionary).has("joint_m"):
			var v: Array = m["joint_m"]
			j = Vector3(float(v[0]), float(v[2]), -float(v[1]))
	var holder := Node3D.new()
	if mirror:
		holder.scale = Vector3(-1, 1, 1)
	joint.add_child(holder)
	holder.add_child(inst)
	inst.position = -j
	return joint


func _build_assets(t: int) -> void:
	var letter := "A" if t <= 0 else "B"
	if _hips:
		_hips.queue_free()
	_legs.clear()
	_arms.clear()
	_weapon = null
	_weapon_id = -1
	_hips = Node3D.new()
	_hips.position = Vector3(0, 0.95, 0)
	add_child(_hips)
	for side in [-1.0, 1.0]:
		var thigh := _part("Operator%sThigh" % letter, side < 0.0)
		thigh.position = Vector3(0.105 * side, 0, 0)
		_hips.add_child(thigh)
		var shin := _part("Operator%sShin" % letter, side < 0.0)
		shin.position = Vector3(0, -0.46, 0)
		thigh.add_child(shin)
		_legs.append(thigh)
		_legs.append(shin)
	_torso = _part("Operator%sTorso" % letter)
	_hips.add_child(_torso)
	_head = _part("Operator%sHead" % letter)
	_head.position = Vector3(0, 0.6, 0)
	_torso.add_child(_head)
	for side in [1.0, -1.0]:
		var upper := _part("Operator%sUpperArm" % letter, side < 0.0)
		upper.position = Vector3(SHOULDER.x * side, SHOULDER.y, SHOULDER.z)
		_torso.add_child(upper)
		var fore := _part("Operator%sForearm" % letter, side < 0.0)
		fore.position = Vector3(0, -UPPER_ARM, 0)
		upper.add_child(fore)
		_arms.append(upper)
		_arms.append(fore)
	_weapon_pivot = Node3D.new()
	_weapon_pivot.position = Vector3(0, 0.5, 0)
	_torso.add_child(_weapon_pivot)


func _make_label() -> void:
	_label = Label3D.new()
	_label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	_label.position = Vector3(0, 2.05, 0)
	_label.font_size = 40
	_label.outline_size = 8
	_label.pixel_size = 0.004
	_label.no_depth_test = false
	add_child(_label)


func _m(key: String, color: Color, rough := 0.8) -> StandardMaterial3D:
	if not _materials.has(key):
		var m := StandardMaterial3D.new()
		m.albedo_color = color
		m.roughness = rough
		_materials[key] = m
	return _materials[key]


func _build() -> void:
	var skin := _m("skin", Color(0.55, 0.4, 0.32), 0.7)
	var boots := _m("boots", Color(0.12, 0.1, 0.08), 0.6)
	var gear := _m("gear", Color(0.18, 0.18, 0.16), 0.85)
	var lens := _m("lens", Color(0.05, 0.06, 0.07), 0.1)
	_hips = Node3D.new()
	_hips.position = Vector3(0, 0.95, 0)
	add_child(_hips)
	# Legs: thigh pivots at the hips, shin pivots at the knees.
	for side in [-1.0, 1.0]:
		var thigh := Node3D.new()
		thigh.position = Vector3(0.11 * side, 0, 0)
		_hips.add_child(thigh)
		TfpsModels.capsule(thigh, 0.085, 0.5, Vector3(0, -0.23, 0), _m("uniform", Color.WHITE))
		TfpsModels.box(thigh, Vector3(0.1, 0.12, 0.06), Vector3(0.06 * side, -0.2, -0.02), gear)  # thigh pouch
		var shin := Node3D.new()
		shin.position = Vector3(0, -0.46, 0)
		thigh.add_child(shin)
		TfpsModels.capsule(shin, 0.07, 0.48, Vector3(0, -0.22, 0), _m("uniform", Color.WHITE))
		TfpsModels.box(shin, Vector3(0.11, 0.12, 0.13), Vector3(0, -0.04, -0.02), gear)  # knee pad
		TfpsModels.box(shin, Vector3(0.11, 0.1, 0.27), Vector3(0, -0.45, -0.05), boots)
		_legs.append(thigh)
		_legs.append(shin)
	# Torso with plate carrier and pouches.
	_torso = Node3D.new()
	_hips.add_child(_torso)
	TfpsModels.capsule(_torso, 0.17, 0.62, Vector3(0, 0.28, 0), _m("uniform", Color.WHITE))
	TfpsModels.box(_torso, Vector3(0.36, 0.42, 0.26), Vector3(0, 0.32, 0), gear)
	for x in [-0.11, 0.0, 0.11]:
		TfpsModels.box(_torso, Vector3(0.08, 0.12, 0.06), Vector3(x, 0.2, -0.15), gear)  # magazine pouches
	TfpsModels.box(_torso, Vector3(0.38, 0.03, 0.27), Vector3(0, 0.5, 0), _m("accent", Color.WHITE))  # team band
	TfpsModels.box(_torso, Vector3(0.22, 0.25, 0.1), Vector3(0, 0.3, 0.17), gear)  # back panel
	# Head: neck, head, helmet, goggles, ear protection.
	_head = Node3D.new()
	_head.position = Vector3(0, 0.66, 0)
	_torso.add_child(_head)
	TfpsModels.capsule(_head, 0.055, 0.14, Vector3(0, 0.02, 0), skin)
	TfpsModels.sphere(_head, 0.11, Vector3(0, 0.15, 0), skin, Vector3(0.9, 1.05, 1.0))
	TfpsModels.sphere(_head, 0.135, Vector3(0, 0.2, 0.01), _m("helmet", Color.WHITE, 0.9), Vector3(1.0, 0.75, 1.05))
	TfpsModels.box(_head, Vector3(0.2, 0.05, 0.05), Vector3(0, 0.17, -0.1), lens)
	for side in [-1.0, 1.0]:
		TfpsModels.cyl(_head, 0.045, 0.04, Vector3(0.12 * side, 0.15, 0), gear, Vector3(0, 0, 90))
	# Arms and weapon: the weapon pivots at the shoulders with the aim pitch.
	_weapon_pivot = Node3D.new()
	_weapon_pivot.position = Vector3(0, 0.5, 0)
	_torso.add_child(_weapon_pivot)
	for side in [-1.0, 1.0]:
		var reach := -0.32 if side > 0.0 else -0.42
		TfpsModels.capsule(_weapon_pivot, 0.055, 0.34, Vector3(0.17 * side, -0.08, -0.1), _m("uniform", Color.WHITE), Vector3(60, 0, -15 * side))
		TfpsModels.capsule(_weapon_pivot, 0.05, 0.3, Vector3(0.09 * side, -0.12, reach + 0.08), _m("uniform", Color.WHITE), Vector3(80, 20 * side, 0))
		TfpsModels.sphere(_weapon_pivot, 0.045, Vector3(0.04 * side, -0.12, reach - 0.06), _m("gloves", Color(0.15, 0.14, 0.12)))
	for mi in find_children("*", "GeometryInstance3D", true, false):
		(mi as GeometryInstance3D).cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON


func set_team(t: int) -> void:
	if t == team:
		return
	team = t
	var i := clampi(t, 0, 1)
	_label.modulate = TEAM_ACCENT[i].lightened(0.3)
	if _use_assets:
		_build_assets(t)
		return
	_materials["uniform"].albedo_color = TEAM_UNIFORM[i]
	_materials["helmet"].albedo_color = TEAM_UNIFORM[i].darkened(0.15)
	var accent: StandardMaterial3D = _materials["accent"]
	accent.albedo_color = TEAM_ACCENT[i]
	accent.emission_enabled = true
	accent.emission = TEAM_ACCENT[i] * 0.4


func set_weapon(id: int) -> void:
	if id == _weapon_id:
		return
	_weapon_id = id
	if _weapon:
		_weapon.queue_free()
		_weapon = null
	if id > 0:
		_weapon = TfpsModels.weapon(TfpsModels.weapon_class(id))
		if not _use_assets:
			_weapon.position = Vector3(0.05, -0.1, -0.28)
		elif TfpsModels.weapon_class(id) == "pistol":
			_weapon.position = Vector3(0.0, -0.02, -0.4)   # both hands, arms extended
		else:
			_weapon.position = Vector3(0.04, -0.08, -0.22)
		_weapon_pivot.add_child(_weapon)


## Applies one replicated sample (Godot space).
func apply(p: Dictionary, delta: float) -> void:
	var pos := TfpsNet.vec(p["position"])
	var flags := int(p["flags"])
	var horizontal := Vector2(pos.x - _last_pos.x, pos.z - _last_pos.z).length()
	if delta > 0.0 and horizontal < 2.0:
		_speed = lerpf(_speed, horizontal / delta, minf(1.0, delta * 10.0))
	_last_pos = pos
	position = pos
	rotation.y = deg_to_rad(-float(p["yaw"]) - 90.0)
	set_team(int(p["team"]))
	set_weapon(int(p.get("weapon", 0)))
	var alive := (flags & TfpsNet.F_ALIVE) != 0
	var k := minf(1.0, delta * 10.0)
	_crouch = lerpf(_crouch, 1.0 if flags & TfpsNet.F_CROUCHED else 0.0, k)
	_prone = lerpf(_prone, 1.0 if flags & TfpsNet.F_PRONE else 0.0, minf(1.0, delta * 5.0))
	_dead = lerpf(_dead, 0.0 if alive else 1.0, minf(1.0, delta * 4.0))
	_lean = lerpf(_lean, float(p.get("lean", 0.0)), k)
	_pitch = lerpf(_pitch, float(p["pitch"]), k)
	var tag: String = p.get("name", "")
	if not alive:
		tag += "  [down]"
	elif p.get("interrupted", false):
		tag += "  [connection lost]"
	_label.text = tag
	_animate(delta, flags)


func _animate(delta: float, flags: int) -> void:
	var on_ground := (flags & TfpsNet.F_ON_GROUND) != 0
	var stride := clampf(_speed / 4.5, 0.0, 1.4)
	_phase += delta * (4.0 + _speed * 1.6) * (1.0 if stride > 0.05 else 0.0)
	var swing := sin(_phase) * 0.55 * stride
	# Legs: alternate swing; knees bend on the back stroke; crouch bends both.
	var crouch_bend := _crouch * 1.1
	for i in 2:
		var s := swing if i == 0 else -swing
		var thigh: Node3D = _legs[i * 2]
		var shin: Node3D = _legs[i * 2 + 1]
		thigh.rotation.x = s - crouch_bend * 0.9 + (0.0 if on_ground else 0.4)
		shin.rotation.x = maxf(0.0, -s) * 1.1 + crouch_bend * 1.6
	_hips.position.y = lerpf(0.95, 0.62, _crouch) - absf(sin(_phase)) * 0.03 * stride
	_torso.rotation.x = -_crouch * 0.25
	_torso.rotation.z = -_lean * 0.3
	_head.rotation.x = deg_to_rad(_pitch) * 0.5
	_weapon_pivot.rotation.x = deg_to_rad(_pitch)
	# Prone and death lay the body down (prone face first, death to the side).
	rotation.x = -_prone * 1.45
	rotation.z = _dead * 1.4
	if _prone > 0.01 or _dead > 0.01:
		_hips.position.y = lerpf(_hips.position.y, 0.2, maxf(_prone, _dead))
	if _use_assets and _arms.size() == 4:
		_solve_arms()


## Places both arms so the right hand holds the grip and the left the
## handguard (or the right hand, for a pistol); elbows point down and out.
func _solve_arms() -> void:
	var tb := _torso.global_basis
	var right_hand: Vector3
	var left_hand: Vector3
	if _weapon:
		right_hand = _weapon.global_position
		var muzzle := _weapon.get_node_or_null("muzzle") as Node3D
		var m := muzzle.position if muzzle else Vector3(0, 0.04, -0.5)
		var pistol := m.length() < 0.25
		var grip_left := Vector3(-0.03, -0.05, 0.02) if pistol else Vector3(0, m.y - 0.04, m.z * 0.55)
		left_hand = _weapon.to_global(grip_left)
	else:
		right_hand = _torso.to_global(Vector3(0.2, 0.0, -0.05))
		left_hand = _torso.to_global(Vector3(-0.2, 0.0, -0.05))
	for i in 2:
		var side := 1.0 if i == 0 else -1.0
		var upper := _arms[i * 2]
		var fore := _arms[i * 2 + 1]
		var shoulder := upper.global_position
		var hand := right_hand if i == 0 else left_hand
		var pole := shoulder + tb * Vector3(side * 0.35, -0.6, 0.25)
		var elbow := _elbow(shoulder, hand, pole, UPPER_ARM, FOREARM)
		upper.global_basis = _limb_basis(shoulder, elbow, tb.x)
		fore.global_position = elbow
		fore.global_basis = _limb_basis(elbow, hand, tb.x)


static func _elbow(shoulder: Vector3, hand: Vector3, pole: Vector3, a: float, b: float) -> Vector3:
	var d := hand - shoulder
	var dist := clampf(d.length(), 0.05, a + b - 0.001)
	var dir := d.normalized() if d.length() > 0.001 else Vector3.DOWN
	var cos_s := clampf((a * a + dist * dist - b * b) / (2.0 * a * dist), -1.0, 1.0)
	var bend := pole - shoulder
	bend -= dir * bend.dot(dir)
	bend = bend.normalized() if bend.length() > 0.001 else Vector3.DOWN
	return shoulder + dir * a * cos_s + bend * a * sqrt(1.0 - cos_s * cos_s)


## Basis for a limb hanging along its -Y axis from `from` toward `to`.
static func _limb_basis(from: Vector3, to: Vector3, right: Vector3) -> Basis:
	var y := (from - to).normalized()
	var z := right.cross(y).normalized()
	if z.length() < 0.5:
		z = Vector3.FORWARD
	var x := y.cross(z).normalized()
	return Basis(x, y, z)
