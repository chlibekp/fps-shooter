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
	if id != _weapon_id:
		_weapon_id = id
		if _weapon:
			_weapon.queue_free()
			_weapon = null
		if id > 0:
			_weapon = TfpsModels.weapon(TfpsModels.weapon_class(id))
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


func muzzle_position() -> Vector3:
	if _weapon:
		var m := _weapon.get_node_or_null("muzzle")
		if m:
			return m.global_position
	return camera.global_position + -camera.global_basis.z * 0.6
