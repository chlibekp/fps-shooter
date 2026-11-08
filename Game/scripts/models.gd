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


## A weapon pointing along -Z (Godot forward), origin at the grip.
## Returns the root; its "muzzle" child marks the barrel end.
static func weapon(weapon_class: String) -> Node3D:
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
	for c in root.get_children():
		if c is GeometryInstance3D:
			c.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON
	return root


## Weapon class for a weapon id from weapons.json.
static func weapon_class(id: int) -> String:
	match id:
		2: return "smg"
		3: return "marksman"
		4: return "pistol"
		5: return "shotgun"
		_: return "rifle"
