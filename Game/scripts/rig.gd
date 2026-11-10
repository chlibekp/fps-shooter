## Procedural posing of an imported skeleton (the Blender operator rig):
## each frame, bones are placed in skeleton space, parents first, by turning
## them from their rest placement (rotate) or pointing them (aim, two-bone
## limb IK); bones not touched this frame follow their parent at rest.
## commit() writes the local poses to the Skeleton3D.
class_name TfpsRig
extends RefCounted

var skel: Skeleton3D
var bone := {}                            # name -> index
var length := {}                          # limb bone -> length to its child joint
var _rest_local: Array[Transform3D] = []
var _rest_global: Array[Transform3D] = []
var _pose: Array[Transform3D] = []
var _touched: Array[bool] = []


func _init(skeleton: Skeleton3D) -> void:
	skel = skeleton
	for i in skel.get_bone_count():
		bone[skel.get_bone_name(i)] = i
		_rest_local.append(skel.get_bone_rest(i))
		_rest_global.append(skel.get_bone_global_rest(i))
		_pose.append(_rest_global[i])
		_touched.append(false)
	for pair in [["upper_arm", "forearm"], ["forearm", "hand"], ["thigh", "shin"], ["shin", "foot"]]:
		for side in ["L", "R"]:
			var a: String = pair[0] + "." + side
			var b: String = pair[1] + "." + side
			if bone.has(a) and bone.has(b):
				length[a] = rest_origin(a).distance_to(rest_origin(b))


func has(name: String) -> bool:
	return bone.has(name)


func rest_origin(name: String) -> Vector3:
	return _rest_global[bone[name]].origin


func rest_basis(name: String) -> Basis:
	return _rest_global[bone[name]].basis


## The bone's placement at rest relative to its parent's current pose.
func base(name: String) -> Transform3D:
	var i: int = bone[name]
	var p := skel.get_bone_parent(i)
	return (_pose[p] if p >= 0 else Transform3D.IDENTITY) * _rest_local[i]


func pose(name: String) -> Transform3D:
	return _pose[bone[name]]


func begin() -> void:
	for i in _touched.size():
		_touched[i] = false


func _put(name: String, t: Transform3D) -> void:
	var i: int = bone[name]
	_pose[i] = t
	_touched[i] = true


## Rest placement turned by `rot` (skeleton space) and moved by `offset`.
func rotate(name: String, rot: Basis, offset := Vector3.ZERO) -> void:
	var b := base(name)
	b.basis = rot * b.basis
	b.origin += offset
	_put(name, b)


func keep(name: String) -> void:
	_put(name, base(name))


## Rest placement with a fixed skeleton-space orientation (e.g. feet level).
func orient(name: String, basis: Basis) -> void:
	var b := base(name)
	b.basis = basis
	_put(name, b)


## Points the bone's +Y along `dir` (skeleton space) with the least twist.
func aim(name: String, dir: Vector3) -> void:
	var b := base(name)
	var cur := b.basis.y.normalized()
	if dir.length() > 0.0001 and cur.dot(dir.normalized()) < 0.99999:
		b.basis = Basis(Quaternion(cur, dir.normalized())) * b.basis
	_put(name, b)


## Two-bone IK: aims `upper` and `lower` so the end reaches `target`,
## bending toward `pole` (skeleton space).
func limb(upper: String, lower: String, target: Vector3, pole: Vector3) -> void:
	keep(upper)
	var root := _pose[bone[upper]].origin
	var mid := two_bone(root, target, pole, length[upper], length[lower])
	aim(upper, mid - root)
	aim(lower, target - mid)


func commit() -> void:
	for i in _pose.size():
		var p := skel.get_bone_parent(i)
		if not _touched[i]:
			_pose[i] = (_pose[p] if p >= 0 else Transform3D.IDENTITY) * _rest_local[i]
		var local := (_pose[p].affine_inverse() if p >= 0 else Transform3D.IDENTITY) * _pose[i]
		skel.set_bone_pose_position(i, local.origin)
		skel.set_bone_pose_rotation(i, local.basis.get_rotation_quaternion())


## Middle joint of a two-bone chain from `root` to `end` (clamped to reach),
## bent toward `pole`.
static func two_bone(root: Vector3, end: Vector3, pole: Vector3, a: float, b: float) -> Vector3:
	var d := end - root
	var dist := clampf(d.length(), 0.05, a + b - 0.001)
	var dir := d.normalized() if d.length() > 0.001 else Vector3.DOWN
	var cos_s := clampf((a * a + dist * dist - b * b) / (2.0 * a * dist), -1.0, 1.0)
	var bend := pole - root
	bend -= dir * bend.dot(dir)
	bend = bend.normalized() if bend.length() > 0.001 else Vector3.DOWN
	return root + dir * a * cos_s + bend * a * sqrt(1.0 - cos_s * cos_s)


## Wrist targets and hand directions (global space) for holding a weapon
## made by TfpsModels.weapon(): the right hand's wrist behind the grip top
## with the fingers wrapping down and forward around the grip; the support
## hand under the handguard (or around the right hand, on a pistol).
static func grip_targets(weapon: Node3D) -> Dictionary:
	var b := weapon.global_basis
	var support := TfpsModels.support_hand(weapon)
	var pistol := support.z > -0.1
	var right_wrist := weapon.to_global(Vector3(0.012, 0.03, 0.055))
	var right_dir := (b * Vector3(-0.25, -0.8, -0.55)).normalized()
	var left_wrist: Vector3
	var left_dir: Vector3
	if pistol:
		left_wrist = weapon.to_global(Vector3(-0.045, -0.02, 0.06))
		left_dir = (b * Vector3(0.45, -0.6, -0.65)).normalized()
	else:
		left_wrist = weapon.to_global(support + Vector3(-0.03, -0.04, 0.07))
		left_dir = (b * Vector3(0.3, 0.3, -0.9)).normalized()
	return {"R": [right_wrist, right_dir], "L": [left_wrist, left_dir]}


## Arms holding a weapon: two-bone IK onto the wrists from grip_targets()
## and hands turned along the given directions (skeleton space via
## `to_skel`). Elbows bend down and out.
func hold(weapon: Node3D, to_skel: Transform3D, left_wrist_override = null) -> void:
	var t := grip_targets(weapon)
	if left_wrist_override != null:
		t["L"] = [left_wrist_override as Vector3, (weapon.global_basis * Vector3(0.2, 0.6, -0.5)).normalized()]
	for side in ["R", "L"]:
		var sgn := 1.0 if side == "R" else -1.0
		var wrist: Vector3 = to_skel * (t[side][0] as Vector3)
		var dir: Vector3 = to_skel.basis * (t[side][1] as Vector3)
		var shoulder := base("upper_arm." + side).origin
		limb("upper_arm." + side, "forearm." + side, wrist, shoulder + Vector3(sgn * 0.45, -0.2, -0.6))
		aim("hand." + side, dir)


## Instances a skinned operator asset with its baked material; returns the
## scene root (its Skeleton3D is found with find_children).
static func instance(asset: String) -> Node3D:
	var path := "res://assets/%s/SM_%s.fbx" % [asset, asset]
	if not ResourceLoader.exists(path):
		return null
	var root: Node3D = (load(path) as PackedScene).instantiate()
	var mat := TfpsProps.material(asset)
	for n in root.find_children("*", "MeshInstance3D", true, false):
		var mi := n as MeshInstance3D
		mi.material_override = mat
		mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON
	return root


static func skeleton_of(root: Node) -> Skeleton3D:
	var found := root.find_children("*", "Skeleton3D", true, false)
	return found[0] as Skeleton3D if found.size() > 0 else null
