## Procedural models built from primitives (original art, no files):
## weapons per class and the operator character.
class_name TfpsModels
extends RefCounted

static var _mats := {}


static func mat(key: String, color: Color, rough := 0.6, metal := 0.0, emission := Color.BLACK) -> StandardMaterial3D:
	if _mats.has(key):
		return _mats[key]
	var m := StandardMaterial3D.new()
	m.albedo_color = color
	m.roughness = rough
	m.metallic = metal
	if emission != Color.BLACK:
		m.emission_enabled = true
		m.emission = emission
	_mats[key] = m
	return m


static func box(parent: Node3D, size: Vector3, pos: Vector3, m: Material, rot := Vector3.ZERO) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	var bm := BoxMesh.new()
	bm.size = size
	mi.mesh = bm
	mi.position = pos
	mi.rotation_degrees = rot
	mi.material_override = m
	parent.add_child(mi)
	return mi


static func cyl(parent: Node3D, radius: float, length: float, pos: Vector3, m: Material, axis := Vector3(0, 0, 90), top := -1.0) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	var cm := CylinderMesh.new()
	cm.top_radius = radius if top < 0.0 else top
	cm.bottom_radius = radius
	cm.height = length
	cm.radial_segments = 16
	mi.mesh = cm
	mi.position = pos
	mi.rotation_degrees = axis
	mi.material_override = m
	parent.add_child(mi)
	return mi


static func capsule(parent: Node3D, radius: float, height: float, pos: Vector3, m: Material, rot := Vector3.ZERO) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	var cm := CapsuleMesh.new()
	cm.radius = radius
	cm.height = height
	cm.radial_segments = 16
	cm.rings = 6
	mi.mesh = cm
	mi.position = pos
	mi.rotation_degrees = rot
	mi.material_override = m
	parent.add_child(mi)
	return mi


static func sphere(parent: Node3D, radius: float, pos: Vector3, m: Material, squash := Vector3.ONE) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	var sm := SphereMesh.new()
	sm.radius = radius
	sm.height = radius * 2.0
	sm.radial_segments = 20
	sm.rings = 10
	mi.mesh = sm
	mi.position = pos
	mi.scale = squash
	mi.material_override = m
	parent.add_child(mi)
	return mi


# weapon class -> Blender asset (Assets/Exports/<asset>, synced into Game/assets)
const WEAPON_ASSETS := {
	"rifle": "WeaponCarbine",
	"smg": "WeaponSMG",
	"marksman": "WeaponMarksman",
	"pistol": "WeaponPistol",
	"shotgun": "WeaponShotgun",
}


## A weapon pointing along -Z (Godot forward), origin at the grip.
## Returns the root; its "muzzle" child marks the barrel end. Uses the Blender
## model when the assets are synced, otherwise a procedural one.
static func weapon(weapon_class: String) -> Node3D:
	var asset: String = WEAPON_ASSETS.get(weapon_class, "WeaponCarbine")
	var model := _asset_weapon(asset)
	if model != null:
		return model
	return procedural_weapon(weapon_class)


static func _asset_weapon(asset: String) -> Node3D:
	var inst := TfpsProps.instance(asset)
	if inst == null:
		return null
	var manifest := {}
	var f := FileAccess.open("res://assets/%s/manifest.json" % asset, FileAccess.READ)
	if f != null:
		var parsed: Variant = JSON.parse_string(f.get_as_text())
		if parsed is Dictionary:
			manifest = parsed
	if not manifest.has("grip_m") or not manifest.has("muzzle_m"):
		inst.free()
		return null
	var grip := _blender_to_godot(manifest["grip_m"])
	var muzzle_pos := _blender_to_godot(manifest["muzzle_m"])
	var root := Node3D.new()
	root.add_child(inst)
	inst.position = -grip
	var muzzle := Node3D.new()
	muzzle.name = "muzzle"
	muzzle.position = muzzle_pos - grip
	root.add_child(muzzle)
	if manifest.has("sight_m"):
		var sight := Node3D.new()
		sight.name = "sight"
		sight.position = _blender_to_godot(manifest["sight_m"]) - grip
		root.add_child(sight)
	return root


## Blender model space (Z up, barrel along +Y) to Godot (Y up, barrel along -Z).
static func _blender_to_godot(v: Array) -> Vector3:
	return Vector3(float(v[0]), float(v[2]), -float(v[1]))


## The procedural weapon (primitives), used when the assets are not synced.
static func procedural_weapon(weapon_class: String) -> Node3D:
	var root := Node3D.new()
	var dark := mat("gun_dark", Color(0.08, 0.085, 0.09), 0.45, 0.6)
	var poly := mat("gun_poly", Color(0.14, 0.14, 0.13), 0.8, 0.0)
	var tan := mat("gun_tan", Color(0.42, 0.36, 0.26), 0.75, 0.0)
	var glass := mat("gun_glass", Color(0.05, 0.08, 0.1), 0.05, 0.3, Color(0.0, 0.05, 0.02))
	var length := 0.62
	var barrel := 0.32
	var mag := Vector3(0.035, 0.16, 0.07)
	var stock := true
	var body := tan
	match weapon_class:
		"smg":
			length = 0.42; barrel = 0.14; mag = Vector3(0.03, 0.2, 0.04); body = poly
		"pistol":
			length = 0.18; barrel = 0.0; mag = Vector3(0.03, 0.08, 0.05); stock = false; body = dark
		"shotgun":
			length = 0.62; barrel = 0.36; mag = Vector3(0.0, 0.0, 0.0); body = poly
		"marksman", "dmr", "sniper":
			length = 0.72; barrel = 0.42; mag = Vector3(0.035, 0.11, 0.08); body = tan
	# Receiver and handguard.
	box(root, Vector3(0.055, 0.075, length), Vector3(0, 0.02, -length * 0.35), body)
	box(root, Vector3(0.05, 0.02, length * 0.9), Vector3(0, 0.067, -length * 0.35), dark)  # top rail
	if barrel > 0.0:
		cyl(root, 0.011, barrel, Vector3(0, 0.035, -length * 0.85 - barrel * 0.5 + 0.05), dark, Vector3(90, 0, 0))
		cyl(root, 0.017, 0.06, Vector3(0, 0.035, -length * 0.85 - barrel + 0.03), dark, Vector3(90, 0, 0))  # muzzle device
	if mag.y > 0.0:
		box(root, mag, Vector3(0, -mag.y * 0.45, -length * 0.25), poly, Vector3(-12, 0, 0))
	if weapon_class == "shotgun":
		cyl(root, 0.022, 0.4, Vector3(0, -0.01, -0.45), poly, Vector3(90, 0, 0))  # pump tube
	# Pistol grip.
	box(root, Vector3(0.035, 0.11, 0.045), Vector3(0, -0.05, 0.02), poly, Vector3(-18, 0, 0))
	if stock:
		box(root, Vector3(0.04, 0.07, 0.24), Vector3(0, 0.0, 0.17), poly)
		box(root, Vector3(0.045, 0.12, 0.03), Vector3(0, -0.01, 0.29), poly)
	# Optic.
	if weapon_class != "pistol" and weapon_class != "shotgun":
		var long_scope := weapon_class in ["marksman", "dmr", "sniper"]
		cyl(root, 0.022 if long_scope else 0.026, 0.2 if long_scope else 0.09, Vector3(0, 0.11, -0.12), dark, Vector3(90, 0, 0))
		box(root, Vector3(0.03, 0.025, 0.03), Vector3(0, 0.087, -0.12), dark)
		sphere(root, 0.02, Vector3(0, 0.11, -0.12 - (0.1 if long_scope else 0.045)), glass, Vector3(1, 1, 0.2))
	else:
		box(root, Vector3(0.01, 0.015, 0.01), Vector3(0, 0.075, -length * 0.6), dark)  # front sight
	var muzzle := Node3D.new()
	muzzle.name = "muzzle"
	muzzle.position = Vector3(0, 0.035, -length * 0.85 - barrel - 0.02)
	root.add_child(muzzle)
	var sight := Node3D.new()
	sight.name = "sight"
	var optic := weapon_class != "pistol" and weapon_class != "shotgun"
	sight.position = Vector3(0, 0.11 if optic else 0.08, -0.12)
	root.add_child(sight)
	for c in root.get_children():
		if c is GeometryInstance3D:
			c.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON
	return root


## Where the support (left) hand holds a weapon made by weapon(), in its
## local space: the handguard, or wrapped around the right hand on a pistol.
static func support_hand(weapon_root: Node3D) -> Vector3:
	var muzzle := weapon_root.get_node_or_null("muzzle") as Node3D
	var m := muzzle.position if muzzle else Vector3(0, 0.04, -0.5)
	if m.length() < 0.25:
		return Vector3(-0.03, -0.05, 0.02)
	return Vector3(0, m.y - 0.04, m.z * 0.55)


## A capsule from `a` to `b` (a forearm, an upper arm).
static func segment(parent: Node3D, a: Vector3, b: Vector3, radius: float, m: Material, end_radius := -1.0) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	var length := a.distance_to(b)
	if end_radius > 0.0:
		var cm := CylinderMesh.new()
		cm.top_radius = end_radius
		cm.bottom_radius = radius
		cm.height = length
		cm.radial_segments = 14
		mi.mesh = cm
	else:
		var cap := CapsuleMesh.new()
		cap.radius = radius
		cap.height = length + radius * 2.0
		cap.radial_segments = 14
		cap.rings = 4
		mi.mesh = cap
	var dir := (b - a).normalized()
	var side := Vector3.RIGHT if absf(dir.dot(Vector3.RIGHT)) < 0.9 else Vector3.FORWARD
	var x := side.cross(dir).normalized()
	mi.transform = Transform3D(Basis(x, dir, x.cross(dir)), (a + b) * 0.5)
	mi.material_override = m
	parent.add_child(mi)
	return mi


## First-person arms holding a weapon made by weapon(): a gloved right hand
## on the grip and the left hand on the handguard (or around the right hand
## for a pistol). Added as children of the weapon, so they move with it.
static func add_arms(weapon_root: Node3D, weapon_class: String, team := 0) -> void:
	# Sleeve colours match the operator uniforms (team A olive, B slate).
	var sleeve := mat("fp_sleeve_b", Color(0.25, 0.27, 0.31), 0.9) if team == 1 else mat("fp_sleeve_a", Color(0.36, 0.38, 0.28), 0.9)
	var glove := mat("fp_glove", Color(0.06, 0.06, 0.055), 0.7)
	var cuff := mat("fp_cuff", Color(0.1, 0.1, 0.09), 0.85)
	var muzzle_node := weapon_root.get_node_or_null("muzzle") as Node3D
	var muzzle := muzzle_node.position if muzzle_node else Vector3(0, 0.04, -0.6)
	# Right hand around the grip (origin), forearm back and down to the right.
	box(weapon_root, Vector3(0.05, 0.085, 0.075), Vector3(0.004, -0.045, 0.012), glove, Vector3(-14, 0, 0))
	box(weapon_root, Vector3(0.022, 0.02, 0.05), Vector3(-0.026, -0.012, -0.03), glove, Vector3(0, 8, 0))  # trigger finger
	var r_wrist := Vector3(0.012, -0.07, 0.075)
	segment(weapon_root, r_wrist, Vector3(0.17, -0.19, 0.4), 0.034, cuff)
	segment(weapon_root, r_wrist + Vector3(0.025, -0.02, 0.06), Vector3(0.32, -0.3, 0.72), 0.05, sleeve)
	# Left hand.
	var l_hand: Vector3
	if weapon_class == "pistol":
		l_hand = Vector3(-0.03, -0.06, 0.02)
	else:
		var along := 0.5 if weapon_class != "smg" else 0.62
		l_hand = Vector3(0.0, muzzle.y - 0.045, muzzle.z * along)
	box(weapon_root, Vector3(0.05, 0.05, 0.09), l_hand + Vector3(-0.01, 0, 0), glove, Vector3(0, 0, 20))
	var l_wrist := l_hand + Vector3(-0.03, -0.04, 0.05)
	segment(weapon_root, l_wrist, l_wrist + Vector3(-0.1, -0.12, 0.3), 0.032, cuff)
	segment(weapon_root, l_wrist + Vector3(-0.02, -0.03, 0.07), l_wrist + Vector3(-0.2, -0.3, 0.62), 0.048, sleeve)


## Weapon class for a weapon id from weapons.json.
static func weapon_class(id: int) -> String:
	match id:
		2: return "smg"
		3: return "marksman"
		4: return "pistol"
		5: return "shotgun"
		_: return "rifle"
