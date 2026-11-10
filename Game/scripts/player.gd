## The local player: mouse look, input sampling and the first-person view
## (camera at the predicted eye, weapon view model with sway, bob, recoil,
## aim-down-sights and reload / equip motion).
class_name TfpsPlayer
extends Node3D

var sensitivity := 0.07        # degrees per mouse count
var ads_sensitivity := 0.65
var invert_y := false
var toggle_crouch := true
var base_fov := 90.0
var input_enabled := true

var yaw := 0.0                 # simulation degrees (0 = +X toward +Y)
var pitch := 0.0
var slot := 0
var camera: Camera3D
var _view: Node3D              # view-model root (child of the camera)
var _weapon: Node3D
var _weapon_id := -1
var _team := 0
var attachments := 0   # primary weapon: barrel bits 0-1, grip bits 2-3
var _arms_root: Node3D
var _arms_rig: TfpsRig
var _arms_asset := ""
var _hand_target = null   # support-hand override during reloads (Vector3 or null)
var _slide_t := 0.0
var _crouch_on := false
var _prone_on := false
var _last_weapon_slot := 0
var _sway := Vector2.ZERO
var _kick := 0.0
var _bob := 0.0
var _last_shot := -1
var _mouse_delta := Vector2.ZERO
var _aim := 0.0


func _ready() -> void:
	camera = Camera3D.new()
	camera.near = 0.03
	camera.far = 600.0
	camera.fov = base_fov
	add_child(camera)
	camera.make_current()
	_view = Node3D.new()
	camera.add_child(_view)


func _unhandled_input(event: InputEvent) -> void:
	if not input_enabled:
		return
	if event is InputEventMouseMotion and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		var s := sensitivity * lerpf(1.0, ads_sensitivity, _aim)
		yaw = fposmod(yaw + event.relative.x * s, 360.0)
		pitch = clampf(pitch - event.relative.y * s * (-1.0 if invert_y else 1.0), -89.0, 89.0)
		_mouse_delta += event.relative
	if event.is_action_pressed("crouch"):
		_crouch_on = not _crouch_on
		_prone_on = false
	if event.is_action_pressed("prone"):
		_prone_on = not _prone_on
		_crouch_on = false
	if event.is_action_pressed("jump") and _prone_on:
		_prone_on = false
	if event.is_action_pressed("primary"):
		slot = 0
	if event.is_action_pressed("secondary"):
		slot = 1
	for i in 4:
		if event.is_action_pressed("gadget%d" % (i + 1)):
			slot = 2 + i
	if event.is_action_pressed("next_weapon") or event.is_action_pressed("prev_weapon"):
		slot = _last_weapon_slot if slot >= 2 else (1 - slot)


func buttons() -> int:
	if not input_enabled:
		return 0
	var b := 0
	var map := {"forward": TfpsNet.FORWARD, "back": TfpsNet.BACK, "left": TfpsNet.LEFT, "right": TfpsNet.RIGHT,
		"jump": TfpsNet.JUMP, "sprint": TfpsNet.SPRINT, "walk": TfpsNet.WALK, "lean_left": TfpsNet.LEAN_LEFT,
		"lean_right": TfpsNet.LEAN_RIGHT, "aim": TfpsNet.AIM, "fire": TfpsNet.FIRE, "reload": TfpsNet.RELOAD,
		"use": TfpsNet.USE}
	for action in map:
		if Input.is_action_pressed(action):
			b |= map[action]
	if Input.is_action_pressed("crouch_hold") or (_crouch_on if toggle_crouch else Input.is_action_pressed("crouch")):
		b |= TfpsNet.CROUCH
	if _prone_on:
		b |= TfpsNet.PRONE
	if slot < 2:
		_last_weapon_slot = slot
	return b


## Places the camera and animates the view model from this frame's state.
func apply(s: Dictionary, delta: float, shake: float) -> void:
	var eye := TfpsNet.vec(s.get("eye", [0, 1.6, 0]))
	var w: Dictionary = s.get("weapon", {})
	var recoil_p := float(w.get("recoil_pitch", 0.0))
	var recoil_y := float(w.get("recoil_yaw", 0.0))
	_aim = float(s.get("aim", 0.0))
	var lean := float(s.get("lean", 0.0))
	var shake_off := Vector3(randf_range(-1, 1), randf_range(-1, 1), 0) * shake * 0.6
	camera.global_position = eye
	camera.rotation = TfpsNet.view_basis_rotation(yaw + recoil_y, pitch + recoil_p) + Vector3(deg_to_rad(shake_off.y), deg_to_rad(shake_off.x), -lean * 0.12)
	camera.fov = lerpf(base_fov, base_fov * 0.72, _aim)
	_update_view_model(s, w, delta)


func _update_view_model(s: Dictionary, w: Dictionary, delta: float) -> void:
	var id := int(w.get("id", 0)) if int(w.get("active", 0)) < 2 else 0
	var team := clampi(int(s.get("side", 0)), 0, 1)   # same as other players' "team"
	if id != _weapon_id or team != _team:
		_weapon_id = id
		_team = team
		if _weapon:
			_weapon.queue_free()
			_weapon = null
		if id > 0:
			_weapon = TfpsModels.weapon(TfpsModels.weapon_class(id))
			if int(w.get("active", 0)) == 0:
				TfpsModels.add_attachments(_weapon, attachments)
			if not _build_arms():
				TfpsModels.add_arms(_weapon, TfpsModels.weapon_class(id), _team)
			for g in _weapon.find_children("*", "GeometryInstance3D", true, false):
				(g as GeometryInstance3D).cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
			_weapon.scale = Vector3.ONE * 0.72
			_view.add_child(_weapon)
	_view.visible = s.get("alive", true) and _weapon != null
	if _weapon == null:
		return
	# Recoil kick when a new shot was fired.
	var sc := int(w.get("shot_counter", 0))
	if _last_shot >= 0 and sc != _last_shot:
		_kick = minf(_kick + 1.0, 1.6)
		_slide_t = 1.0
	_last_shot = sc
	_kick = move_toward(_kick, 0.0, delta * 9.0)
	# Sway follows the mouse, bob follows walking speed.
	_sway = _sway.lerp(_mouse_delta * 0.0015, minf(1.0, delta * 12.0))
	_mouse_delta = Vector2.ZERO
	var vel := TfpsNet.vec(s.get("velocity", [0, 0, 0]))
	var speed := Vector2(vel.x, vel.z).length()
	_bob += delta * speed * 2.2
	var bob_amt := clampf(speed / 5.0, 0.0, 1.0) * (1.0 - _aim * 0.85) * (1.0 if s.get("on_ground", true) else 0.0)
	var hip := Vector3(0.13, -0.15, -0.36)
	# Aiming puts the sight (optic centre or iron-sight line) on the view axis.
	var sight_node := _weapon.get_node_or_null("sight") as Node3D
	var sight := sight_node.position if sight_node else Vector3(0, 0.11, 0)
	var ads := Vector3(-sight.x * 0.72, -sight.y * 0.72, -0.31)
	var pos := hip.lerp(ads, _aim)
	pos += Vector3(sin(_bob) * 0.012, -absf(cos(_bob)) * 0.01, 0) * bob_amt
	pos += Vector3(-_sway.x, _sway.y, 0) * (1.0 - _aim * 0.7)
	pos.z += _kick * 0.035
	var rot := Vector3(_kick * 4.0 + _sway.y * 30.0, -_sway.x * 30.0 + 4.0 * (1.0 - _aim), -_sway.x * 20.0)
	# Reload: dip and roll the weapon; equip: raise it from below.
	var rp := float(w.get("reload_progress", 0.0)) if w.get("reloading", false) else 0.0
	var dip := sin(rp * PI)
	pos.y -= dip * 0.12
	rot += Vector3(-dip * 25.0, 0, dip * 35.0)
	var equip := clampf(float(w.get("equip_timer", 0.0)) * 2.0, 0.0, 1.0)
	pos.y -= equip * 0.25
	_weapon.position = pos
	_weapon.rotation_degrees = rot
	_animate_magazine(rp)
	_animate_slide(w, delta)
	_pose_arms()


## A pistol's slide snaps back on each shot and returns; it stays locked
## back while the magazine is empty (until the reload).
func _animate_slide(w: Dictionary, delta: float) -> void:
	var slide := _weapon.find_child("*_Slide", true, false) as Node3D
	if slide == null:
		return
	if not slide.has_meta("rest"):
		slide.set_meta("rest", slide.position)
	_slide_t = move_toward(_slide_t, 0.0, delta * 14.0)
	var empty: bool = int(w.get("ammo", 1)) == 0 and not bool(w.get("reloading", false))
	var back := 1.0 if empty else sin(_slide_t * PI * 0.5)
	slide.position = (slide.get_meta("rest") as Vector3) + Vector3(0, 0, 0.035 * back)


## Reload: the magazine drops out, the support hand brings a fresh one up
## from the vest and seats it (weapons whose magazine is a separate mesh).
func _animate_magazine(rp: float) -> void:
	var mag := _weapon.find_child("*_Mag", true, false) as Node3D
	_hand_target = null
	if mag == null:
		return
	if not mag.has_meta("rest"):
		mag.set_meta("rest", mag.transform)
	var rest: Transform3D = mag.get_meta("rest")
	var drop := Vector3.ZERO
	var visible_now := true
	if rp > 0.08 and rp < 0.34:
		var k := smoothstep(0.08, 0.34, rp)
		drop = Vector3(0, -0.38 * k * k, 0.04 * k)
	elif rp >= 0.34 and rp < 0.45:
		visible_now = false
	elif rp >= 0.45 and rp < 0.8:
		var k := 1.0 - smoothstep(0.45, 0.8, rp)
		drop = Vector3(-0.03 * k, -0.18 * k, 0.05 * k)
	mag.transform = Transform3D(rest.basis, rest.origin + drop / maxf(_weapon.scale.x, 0.01))
	mag.visible = visible_now
	# The support hand holds the magazine while it comes in (and reaches for
	# the vest while it is out of sight).
	if rp > 0.36 and rp < 0.86 and mag is MeshInstance3D:
		var mi := mag as MeshInstance3D
		var bottom := mi.get_aabb().get_center() - Vector3(0, mi.get_aabb().size.y * 0.35, 0)
		_hand_target = mi.global_transform * bottom if visible_now else _weapon.to_global(Vector3(-0.15, -0.35, 0.15))


## First-person arms: the team's skinned operator sleeves and gloves
## (OperatorAArms / OperatorBArms), shoulders just below the camera, hands
## placed on the weapon by two-bone IK every frame. False when the asset
## is not synced (the procedural arms are used instead).
func _build_arms() -> bool:
	var asset := "OperatorBArms" if _team == 1 else "OperatorAArms"
	if _arms_root and _arms_asset == asset:
		return true
	if _arms_root:
		_arms_root.queue_free()
		_arms_root = null
		_arms_rig = null
	var inst := TfpsRig.instance(asset)
	if inst == null:
		return false
	for g in inst.find_children("*", "GeometryInstance3D", true, false):
		(g as GeometryInstance3D).cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_arms_root = inst
	_arms_asset = asset
	# The model's eyes (about 1.665 m up, 9 cm forward) near the camera; arms
	# slightly long, as is usual for a first-person view.
	_arms_root.scale = Vector3.ONE * 1.12
	_arms_root.position = Vector3(0, -1.665 * 1.12 + 0.02, 0.09 * 1.12 - 0.17)
	_view.add_child(_arms_root)
	_arms_rig = TfpsRig.new(TfpsRig.skeleton_of(_arms_root))
	return true


func _pose_arms() -> void:
	if _arms_rig == null or _weapon == null:
		return
	var r := _arms_rig
	r.begin()
	r.hold(_weapon, r.skel.global_transform.affine_inverse(), _hand_target)
	r.commit()


## Where spent casings leave the weapon: the right side of the receiver.
func ejection_position() -> Vector3:
	if _weapon:
		return _weapon.to_global(Vector3(0.03, 0.075, -0.07))
	return camera.global_position + camera.global_basis * Vector3(0.12, -0.1, -0.4)


func muzzle_position() -> Vector3:
	if _weapon:
		var m := _weapon.get_node_or_null("muzzle")
		if m:
			return m.global_position
	return camera.global_position + -camera.global_basis.z * 0.6
