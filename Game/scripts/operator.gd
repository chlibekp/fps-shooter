## Another player, animated from the replicated state: walk cycle from
## speed, crouch, prone, lean, aim pitch, death. Uses the skinned Blender
## operator (Assets/Exports/Operator<A|B>, one per team) when it is synced:
## its skeleton is posed procedurally, legs and arms by two-bone IK (feet
## on the ground, hands on the weapon). Otherwise a figure built from
## primitives.
class_name TfpsOperator
extends Node3D

const TEAM_UNIFORM := [Color(0.33, 0.35, 0.24), Color(0.17, 0.2, 0.27)]
const TEAM_ACCENT := [Color(0.95, 0.55, 0.12), Color(0.2, 0.55, 0.95)]

var player_id := 0
var team := -1
var _hips: Node3D
var _torso: Node3D
var _head: Node3D
var _legs: Array[Node3D] = []   # primitive figure: [left thigh, left shin, right thigh, right shin]
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
var _skinned := false

var _reload_t := 0.0
var _reload_hand = null   # support-hand target while reloading (Vector3 or null)

# Skinned character.
var _char: Node3D
var _rig: TfpsRig


func _init() -> void:
	_make_label()
	_skinned = ResourceLoader.exists("res://assets/OperatorA/SM_OperatorA.fbx") and ResourceLoader.exists("res://assets/OperatorB/SM_OperatorB.fbx")
	if _skinned:
		_weapon_pivot = Node3D.new()
		add_child(_weapon_pivot)
	else:
		_build()


func _make_label() -> void:
	_label = Label3D.new()
	_label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	_label.position = Vector3(0, 2.05, 0)
	_label.font_size = 40
	_label.outline_size = 8
	_label.pixel_size = 0.004
	_label.no_depth_test = false
	add_child(_label)


# ---------------------------------------------------------------------------
# Skinned character
# ---------------------------------------------------------------------------

func _build_skinned(t: int) -> void:
	if _char:
		_char.queue_free()
	_char = TfpsRig.instance("OperatorB" if t == 1 else "OperatorA")
	add_child(_char)
	_rig = TfpsRig.new(TfpsRig.skeleton_of(_char))


func _animate_skinned(stride: float, on_ground: bool) -> void:
	# Skeleton space is the model's own (faces +Y, Z up, right side +X).
	var r := _rig
	var to_skel := r.skel.global_transform.affine_inverse()
	var fwd := Vector3(0, 1, 0)
	var up := Vector3(0, 0, 1)
	r.begin()
	# Hips lowered by crouching and the walk bounce; spine lean; pitch shared
	# by chest, neck and head.
	var drop := _crouch * 0.38 + absf(sin(_phase)) * 0.03 * stride
	r.rotate("hips", Basis(Vector3.RIGHT, -_crouch * 0.12), -up * drop)
	r.rotate("spine", Basis(fwd, _lean * 0.18) * Basis(Vector3.RIGHT, _crouch * 0.25))
	var pitch := deg_to_rad(_pitch)
	r.rotate("chest", Basis(fwd, _lean * 0.18) * Basis(Vector3.RIGHT, pitch * 0.35))
	r.rotate("neck", Basis(Vector3.RIGHT, pitch * 0.25))
	r.rotate("head", Basis(Vector3.RIGHT, pitch * 0.3))
	# Legs: feet step forward and back, lifting on the forward swing; IK
	# keeps them planted otherwise, so crouching bends the knees.
	for side in ["L", "R"]:
		var sgn := 1.0 if side == "R" else -1.0
		var ph := _phase + (0.0 if side == "L" else PI)
		var step := sin(ph) * 0.28 * stride
		var lift := maxf(0.0, cos(ph)) * 0.12 * stride + (0.0 if on_ground else 0.18)
		var ankle := r.rest_origin("foot." + side) + fwd * (step + _crouch * 0.05) + up * lift + Vector3(sgn * _crouch * 0.05, 0, 0)
		var hip := r.base("thigh." + side).origin
		r.limb("thigh." + side, "shin." + side, ankle, hip + fwd * 0.6 - up * 0.1)
		r.orient("foot." + side, r.rest_basis("foot." + side))
	# Arms: hands on the weapon, or hanging relaxed without one.
	r.keep("clavicle.L")
	r.keep("clavicle.R")
	if _weapon:
		r.hold(_weapon, to_skel, _reload_hand)
	r.commit()


# ---------------------------------------------------------------------------
# Primitive figure (fallback)
# ---------------------------------------------------------------------------

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
	if _skinned:
		_build_skinned(i)
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
		if not _skinned:
			_weapon.position = Vector3(0.05, -0.1, -0.28)
		elif TfpsModels.weapon_class(id) == "pistol":
			_weapon.position = Vector3(0.03, -0.08, -0.42)   # both hands, arms extended
		else:
			_weapon.position = Vector3(0.09, -0.06, -0.3)   # stock in the right shoulder
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
	# The weapon pivots at the shoulders with the aim pitch and lean.
	var shoulder_y := 1.42 - _crouch * 0.4
	if _skinned:
		_weapon_pivot.position = Vector3(0, shoulder_y, 0)
	else:
		_weapon_pivot.position = Vector3(0, 0.5, 0)
	# Reloading (replicated flag): the weapon tips down and rolls while the
	# magazine is swapped; the support hand follows the magazine.
	var reloading := (flags & TfpsNet.F_RELOADING) != 0
	_reload_t = _reload_t + delta if reloading else 0.0
	var r := clampf(_reload_t / 2.2, 0.0, 1.0)
	var dip := sin(r * PI) if reloading else 0.0
	_weapon_pivot.rotation.x = deg_to_rad(_pitch) - dip * 0.35
	_weapon_pivot.rotation.z = -_lean * 0.3 + dip * 0.45
	_reload_hand = null
	if _weapon:
		var mag := _weapon.find_child("*_Mag", true, false) as Node3D
		if mag != null:
			if not mag.has_meta("rest"):
				mag.set_meta("rest", mag.transform)
			var rest: Transform3D = mag.get_meta("rest")
			var drop := 0.0
			if reloading and r > 0.1 and r < 0.35:
				drop = smoothstep(0.1, 0.35, r) * 0.4
			elif reloading and r >= 0.35 and r < 0.8:
				drop = (1.0 - smoothstep(0.45, 0.8, r)) * 0.25
			mag.transform = Transform3D(rest.basis, rest.origin - Vector3(0, drop, 0))
			mag.visible = not (reloading and r >= 0.35 and r < 0.45)
			if reloading and r > 0.3 and r < 0.85 and mag is MeshInstance3D:
				var mi := mag as MeshInstance3D
				_reload_hand = mi.global_transform * (mi.get_aabb().get_center() - Vector3(0, mi.get_aabb().size.y * 0.3, 0))
	# Prone and death lay the body down (prone face first, death to the side).
	rotation.x = -_prone * 1.45
	rotation.z = _dead * 1.4
	if _skinned:
		if _rig:
			_animate_skinned(stride, on_ground)
	else:
		_animate_primitive(flags, stride)


func _animate_primitive(flags: int, stride: float) -> void:
	var on_ground := (flags & TfpsNet.F_ON_GROUND) != 0
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
	if _prone > 0.01 or _dead > 0.01:
		_hips.position.y = lerpf(_hips.position.y, 0.2, maxf(_prone, _dead))
