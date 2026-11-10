## Furniture and fittings drawn in place of a map solid's box (the solid
## stays the gameplay collision; when it starts to break, the model is
## replaced by the cells). Each model fills the solid's box: origin at the
## bottom centre, long side along local X.
class_name TfpsFurniture
extends RefCounted

static var _mats := {}
static var _box := BoxMesh.new()
static var _cyl: CylinderMesh
static var _sph: SphereMesh

const MODELLED := ["desk", "conference_table", "dining_table", "shelf", "archive_shelf", "bookcase",
	"rack", "server_rack", "wardrobe", "bed", "sofa", "counter", "kitchen_island", "reception",
	"wine_rack", "bollard", "planter", "atrium_planter", "hedge", "column", "car", "car_cabin",
	"dumpster", "railing", "balustrade", "fountain"]


static func model(tag: String, box: AABB) -> Node3D:
	if not MODELLED.has(tag):
		return null
	var root := Node3D.new()
	var along_x := box.size.x >= box.size.z
	var size := Vector3(box.size.x, box.size.y, box.size.z) if along_x else Vector3(box.size.z, box.size.y, box.size.x)
	root.position = Vector3(box.get_center().x, box.position.y, box.get_center().z)
	if not along_x:
		root.rotation.y = PI * 0.5
	var rng := RandomNumberGenerator.new()
	rng.seed = absi(hash(Vector3i((box.position * 10.0).round())))
	match tag:
		"desk", "conference_table", "dining_table", "reception":
			_table(root, size, tag, rng)
		"shelf", "archive_shelf", "bookcase":
			_shelving(root, size, tag, rng)
		"rack":
			_pallet_rack(root, size, rng)
		"server_rack":
			_server_rack(root, size, rng)
		"wardrobe":
			_wardrobe(root, size)
		"bed":
			_bed(root, size)
		"sofa":
			_sofa(root, size, rng)
		"counter", "kitchen_island":
			_counter(root, size)
		"wine_rack":
			_wine_rack(root, size, rng)
		"bollard":
			_bollard(root, size)
		"planter", "atrium_planter":
			_planter(root, size, rng)
		"hedge":
			_hedge(root, size, rng)
		"column":
			_column(root, size)
		"car":
			_car(root, size, rng)
		"car_cabin":
			_car_cabin(root, size)
		"dumpster":
			_dumpster(root, size)
		"railing", "balustrade":
			_railing(root, size, tag)
		"fountain":
			_fountain(root, size)
	for g in root.find_children("*", "GeometryInstance3D", true, false):
		(g as GeometryInstance3D).cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON
	return root


# ---------------------------------------------------------------------------
# Materials and primitives
# ---------------------------------------------------------------------------

static func mat(key: String, color: Color, rough := 0.7, metal := 0.0, emission := Color.BLACK) -> Material:
	if _mats.has(key):
		return _mats[key]
	var m := StandardMaterial3D.new()
	m.albedo_color = color
	m.roughness = rough
	m.metallic = metal
	if emission != Color.BLACK:
		m.emission_enabled = true
		m.emission = emission
		m.emission_energy_multiplier = 1.5
	_mats[key] = m
	return m


static func world(id: int, tint := Color(1, 1, 1)) -> Array:
	return [TfpsMaterials.get_material(id), tint]


static func b(root: Node3D, size: Vector3, centre: Vector3, m) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	mi.mesh = _box
	mi.scale = size
	mi.position = centre
	_apply(mi, m)
	root.add_child(mi)
	return mi


static func c(root: Node3D, radius: float, height: float, centre: Vector3, m, axis := Vector3.UP, segments := 16) -> MeshInstance3D:
	if _cyl == null:
		_cyl = CylinderMesh.new()
		_cyl.top_radius = 0.5
		_cyl.bottom_radius = 0.5
		_cyl.height = 1.0
		_cyl.radial_segments = 20
		_cyl.rings = 1
	var mi := MeshInstance3D.new()
	mi.mesh = _cyl
	var y := axis.normalized()
	var x := y.cross(Vector3.FORWARD if absf(y.z) < 0.9 else Vector3.RIGHT).normalized()
	var z := x.cross(y)
	mi.transform = Transform3D(Basis(x * radius * 2.0, y * height, z * radius * 2.0), centre)
	_apply(mi, m)
	root.add_child(mi)
	return mi


static var _leaf_sph: SphereMesh


static func s(root: Node3D, radii: Vector3, centre: Vector3, m) -> MeshInstance3D:
	if _sph == null:
		_sph = SphereMesh.new()
		_sph.radius = 0.5
		_sph.height = 1.0
		_sph.radial_segments = 16
		_sph.rings = 8
	var mi := MeshInstance3D.new()
	mi.mesh = _sph
	mi.scale = radii * 2.0
	mi.position = centre
	_apply(mi, m)
	root.add_child(mi)
	return mi


static func _apply(mi: MeshInstance3D, m) -> void:
	if m is Array:
		mi.material_override = m[0]
		mi.set_instance_shader_parameter("tint", m[1])
	else:
		mi.material_override = m


static func foliage() -> Material:
	if not _mats.has("foliage"):
		var m := ShaderMaterial.new()
		m.shader = load("res://shaders/backdrop.gdshader")
		m.set_shader_parameter("foliage", true)
		_mats["foliage"] = m
	return _mats["foliage"]


# ---------------------------------------------------------------------------
# Models (L = size.x along local X, H = size.y, D = size.z)
# ---------------------------------------------------------------------------

static func _table(root: Node3D, sz: Vector3, tag: String, rng: RandomNumberGenerator) -> void:
	var top_t := 0.04 if tag != "conference_table" else 0.06
	var wood := world(TfpsMaterials.WOOD, Color(0.8, 0.72, 0.62) if tag == "conference_table" else Color(1.0, 0.95, 0.9))
	if tag == "reception":
		b(root, Vector3(sz.x, sz.y - 0.05, sz.z * 0.9), Vector3(0, (sz.y - 0.05) * 0.5, 0), mat("reception_panel", Color(0.82, 0.8, 0.76), 0.5))
		b(root, Vector3(sz.x + 0.04, 0.05, sz.z + 0.04), Vector3(0, sz.y - 0.025, 0), mat("stone_top", Color(0.2, 0.2, 0.21), 0.25))
		b(root, Vector3(sz.x * 0.9, 0.02, 0.01), Vector3(0, sz.y * 0.55, sz.z * 0.45 + 0.006), mat("brass", Color(0.7, 0.55, 0.3), 0.3, 1.0))
		return
	b(root, Vector3(sz.x, top_t, sz.z), Vector3(0, sz.y - top_t * 0.5, 0), wood)
	if tag == "desk":
		# Two drawer pedestals and a modesty panel.
		for sx in [-1.0, 1.0]:
			var px: float = sx * (sz.x * 0.5 - 0.22)
			b(root, Vector3(0.42, sz.y - top_t, sz.z * 0.92), Vector3(px, (sz.y - top_t) * 0.5, 0), mat("desk_body", Color(0.55, 0.56, 0.57), 0.6))
			for k in 3:
				var y := 0.12 + k * (sz.y - top_t - 0.1) / 3.0
				b(root, Vector3(0.38, 0.005, 0.01), Vector3(px, y, sz.z * 0.46 + 0.005), mat("desk_line", Color(0.3, 0.3, 0.31), 0.6))
				b(root, Vector3(0.12, 0.015, 0.02), Vector3(px, y + 0.09, sz.z * 0.46 + 0.012), mat("handle", Color(0.7, 0.7, 0.7), 0.3, 1.0))
		b(root, Vector3(sz.x - 0.9, (sz.y - top_t) * 0.6, 0.02), Vector3(0, sz.y - top_t - (sz.y - top_t) * 0.3, -sz.z * 0.4), mat("desk_body", Color(0.55, 0.56, 0.57), 0.6))
		# Monitor and keyboard.
		var mx := rng.randf_range(-0.2, 0.2) * sz.x
		b(root, Vector3(0.55, 0.34, 0.03), Vector3(mx, sz.y + 0.27, -sz.z * 0.2), mat("screen", Color(0.03, 0.03, 0.035), 0.2))
		b(root, Vector3(0.05, 0.1, 0.05), Vector3(mx, sz.y + 0.05, -sz.z * 0.2), mat("plastic_dark", Color(0.05, 0.05, 0.05), 0.5))
		b(root, Vector3(0.45, 0.02, 0.15), Vector3(mx, sz.y + 0.01, sz.z * 0.1), mat("plastic_dark", Color(0.05, 0.05, 0.05), 0.5))
	elif tag == "conference_table":
		for sx in [-0.3, 0.3]:
			b(root, Vector3(0.5, sz.y - top_t, sz.z * 0.4), Vector3(sx * sz.x, (sz.y - top_t) * 0.5, 0), mat("desk_body", Color(0.55, 0.56, 0.57), 0.6))
	else:
		for sx in [-1.0, 1.0]:
			for sz_ in [-1.0, 1.0]:
				b(root, Vector3(0.06, sz.y - top_t, 0.06), Vector3(sx * (sz.x * 0.5 - 0.08), (sz.y - top_t) * 0.5, sz_ * (sz.z * 0.5 - 0.08)), wood)


static func _shelving(root: Node3D, sz: Vector3, tag: String, rng: RandomNumberGenerator) -> void:
	var metal := tag != "bookcase"
	var frame = mat("shelf_steel", Color(0.32, 0.34, 0.36), 0.5, 0.6) if metal else world(TfpsMaterials.WOOD, Color(0.7, 0.55, 0.45))
	var posts := maxi(2, int(sz.x / 0.9) + 1)
	for i in posts:
		var x := -sz.x * 0.5 + 0.03 + i * (sz.x - 0.06) / float(posts - 1)
		for zz in [-1.0, 1.0]:
			b(root, Vector3(0.04, sz.y, 0.04), Vector3(x, sz.y * 0.5, zz * (sz.z * 0.5 - 0.02)), frame)
	var levels := maxi(3, int(sz.y / 0.42))
	var colours := [Color(0.62, 0.48, 0.3), Color(0.15, 0.25, 0.45), Color(0.55, 0.12, 0.1), Color(0.8, 0.78, 0.72), Color(0.2, 0.35, 0.22), Color(0.1, 0.1, 0.1)]
	for l in levels:
		var y := 0.08 + l * (sz.y - 0.1) / float(levels - 1)
		b(root, Vector3(sz.x, 0.025, sz.z - 0.02), Vector3(0, y, 0), frame)
		if l == levels - 1:
			break
		# Stock: cardboard boxes, binders or books.
		var x := -sz.x * 0.5 + 0.05
		while x < sz.x * 0.5 - 0.1:
			var w := rng.randf_range(0.04, 0.09) if tag == "bookcase" else rng.randf_range(0.25, 0.5)
			var h := rng.randf_range(0.18, 0.3)
			if rng.randf() < 0.85 and x + w < sz.x * 0.5 - 0.05:
				var col: Color = colours[rng.randi() % colours.size()] if tag != "shelf" else Color(0.62, 0.48, 0.3).darkened(rng.randf() * 0.25)
				var key := "stock_%d" % (int(col.r * 20) * 400 + int(col.g * 20) * 20 + int(col.b * 20))
				b(root, Vector3(w - 0.01, h, sz.z * rng.randf_range(0.6, 0.85)), Vector3(x + w * 0.5, y + 0.0125 + h * 0.5, 0), mat(key, col, 0.85))
			x += w + rng.randf_range(0.0, 0.04)


static func _pallet_rack(root: Node3D, sz: Vector3, rng: RandomNumberGenerator) -> void:
	var blue := mat("rack_blue", Color(0.08, 0.2, 0.45), 0.5, 0.3)
	var orange := mat("rack_orange", Color(0.85, 0.35, 0.05), 0.45, 0.3)
	var bays := maxi(1, int(round(sz.x / 1.8)))
	for i in bays + 1:
		var x := -sz.x * 0.5 + 0.04 + i * (sz.x - 0.08) / float(bays)
		for zz in [-1.0, 1.0]:
			b(root, Vector3(0.08, sz.y, 0.08), Vector3(x, sz.y * 0.5, zz * (sz.z * 0.5 - 0.04)), blue)
		for k in 6:
			var y := 0.2 + k * (sz.y - 0.3) / 5.0
			b(root, Vector3(0.03, 0.03, sz.z - 0.08), Vector3(x, y, 0), blue)
	for level in [0.0, 1.45]:
		if level > sz.y - 0.5:
			continue
		for zz in [-1.0, 1.0]:
			b(root, Vector3(sz.x, 0.1, 0.05), Vector3(0, level + 0.12, zz * (sz.z * 0.5 - 0.06)), orange)
		for i in bays:
			var x := -sz.x * 0.5 + (i + 0.5) * sz.x / float(bays)
			b(root, Vector3(1.2, 0.14, sz.z - 0.1), Vector3(x, level + 0.24, 0), world(TfpsMaterials.WOOD, Color(0.9, 0.8, 0.65)))
			if rng.randf() < 0.85:
				var h := rng.randf_range(0.5, 1.0)
				b(root, Vector3(1.1, h, sz.z - 0.18), Vector3(x, level + 0.31 + h * 0.5, 0), mat("cardboard", Color(0.55, 0.42, 0.27), 0.9))
				b(root, Vector3(1.11, 0.04, 0.05), Vector3(x, level + 0.31 + h * 0.62, (sz.z - 0.18) * 0.5), mat("tape", Color(0.75, 0.68, 0.5), 0.6))


static func _server_rack(root: Node3D, sz: Vector3, rng: RandomNumberGenerator) -> void:
	b(root, sz, Vector3(0, sz.y * 0.5, 0), mat("rack_black", Color(0.04, 0.04, 0.045), 0.45, 0.4))
	var units := int((sz.y - 0.2) / 0.09)
	for u in units:
		var y := 0.1 + u * 0.09 + 0.045
		b(root, Vector3(sz.x - 0.08, 0.075, 0.01), Vector3(0, y, sz.z * 0.5 + 0.005), mat("server_face", Color(0.12, 0.12, 0.13), 0.4, 0.5))
		if rng.randf() < 0.7:
			var led := mat("led_g", Color(0.1, 1.0, 0.3), 0.3, 0.0, Color(0.1, 1.0, 0.3)) if rng.randf() < 0.8 else mat("led_a", Color(1.0, 0.6, 0.1), 0.3, 0.0, Color(1.0, 0.6, 0.1))
			b(root, Vector3(0.012, 0.012, 0.005), Vector3(-sz.x * 0.35 + rng.randf() * 0.1, y, sz.z * 0.5 + 0.012), led)


static func _wardrobe(root: Node3D, sz: Vector3) -> void:
	var wood := world(TfpsMaterials.WOOD, Color(0.75, 0.62, 0.5))
	b(root, sz, Vector3(0, sz.y * 0.5, 0), wood)
	b(root, Vector3(0.006, sz.y - 0.1, 0.01), Vector3(0, sz.y * 0.5, sz.z * 0.5 + 0.005), mat("gap", Color(0.05, 0.04, 0.03), 0.9))
	for sx in [-1.0, 1.0]:
		b(root, Vector3(0.015, 0.2, 0.025), Vector3(sx * 0.05, sz.y * 0.52, sz.z * 0.5 + 0.015), mat("handle", Color(0.7, 0.7, 0.7), 0.3, 1.0))


static func _bed(root: Node3D, sz: Vector3) -> void:
	var wood := world(TfpsMaterials.WOOD, Color(0.6, 0.45, 0.35))
	b(root, Vector3(sz.x, 0.3, sz.z), Vector3(0, 0.15, 0), wood)
	b(root, Vector3(sz.x - 0.08, 0.22, sz.z - 0.08), Vector3(0, 0.41, 0), mat("mattress", Color(0.88, 0.88, 0.86), 0.9))
	b(root, Vector3(sz.x * 0.62, 0.06, sz.z - 0.02), Vector3(sz.x * 0.17, 0.54, 0), mat("blanket", Color(0.22, 0.3, 0.42), 0.95))
	for zz in [-0.25, 0.25]:
		s(root, Vector3(0.18, 0.06, 0.28), Vector3(-sz.x * 0.38, 0.56, zz * sz.z), mat("pillow", Color(0.92, 0.92, 0.9), 0.9))
	b(root, Vector3(0.06, maxf(sz.y, 0.9), sz.z), Vector3(-sz.x * 0.5 + 0.03, maxf(sz.y, 0.9) * 0.5, 0), wood)


static func _sofa(root: Node3D, sz: Vector3, rng: RandomNumberGenerator) -> void:
	var fabric := mat("sofa_%d" % (rng.randi() % 3), [Color(0.3, 0.32, 0.35), Color(0.45, 0.38, 0.3), Color(0.25, 0.3, 0.25)][rng.randi() % 3], 0.95)
	b(root, Vector3(sz.x, 0.25, sz.z), Vector3(0, 0.17, 0), fabric)
	b(root, Vector3(sz.x, sz.y - 0.05, 0.2), Vector3(0, (sz.y - 0.05) * 0.5 + 0.05, -sz.z * 0.5 + 0.1), fabric)
	for sx in [-1.0, 1.0]:
		b(root, Vector3(0.18, sz.y * 0.75, sz.z), Vector3(sx * (sz.x * 0.5 - 0.09), sz.y * 0.375 + 0.03, 0), fabric)
	var n := maxi(2, int(sz.x / 0.7))
	for i in n:
		var w := (sz.x - 0.36) / float(n)
		b(root, Vector3(w - 0.02, 0.14, sz.z - 0.28), Vector3(-sz.x * 0.5 + 0.18 + (i + 0.5) * w, 0.36, 0.06), fabric)
	for sx in [-1.0, 1.0]:
		for zz in [-1.0, 1.0]:
			b(root, Vector3(0.05, 0.05, 0.05), Vector3(sx * (sz.x * 0.5 - 0.08), 0.025, zz * (sz.z * 0.5 - 0.08)), mat("plastic_dark", Color(0.05, 0.05, 0.05), 0.5))


static func _counter(root: Node3D, sz: Vector3) -> void:
	b(root, Vector3(sz.x, sz.y - 0.04, sz.z - 0.04), Vector3(0, (sz.y - 0.04) * 0.5, 0), mat("cabinet", Color(0.86, 0.85, 0.82), 0.45))
	b(root, Vector3(sz.x + 0.03, 0.04, sz.z + 0.02), Vector3(0, sz.y - 0.02, 0), mat("stone_top", Color(0.2, 0.2, 0.21), 0.25))
	b(root, Vector3(sz.x, 0.08, sz.z - 0.1), Vector3(0, 0.04, -0.03), mat("plastic_dark", Color(0.05, 0.05, 0.05), 0.5))
	var doors := maxi(1, int(sz.x / 0.6))
	for i in doors:
		var x := -sz.x * 0.5 + (i + 0.5) * sz.x / float(doors)
		for zz in [-1.0, 1.0]:
			b(root, Vector3(0.004, sz.y - 0.2, 0.01), Vector3(x - sz.x / float(doors) * 0.5, sz.y * 0.5, zz * (sz.z * 0.5 - 0.015)), mat("gap", Color(0.05, 0.04, 0.03), 0.9))
			b(root, Vector3(0.12, 0.015, 0.02), Vector3(x, sz.y - 0.15, zz * (sz.z * 0.5 - 0.005)), mat("handle", Color(0.7, 0.7, 0.7), 0.3, 1.0))


static func _wine_rack(root: Node3D, sz: Vector3, rng: RandomNumberGenerator) -> void:
	var wood := world(TfpsMaterials.WOOD, Color(0.55, 0.4, 0.3))
	b(root, Vector3(sz.x, sz.y, 0.03), Vector3(0, sz.y * 0.5, -sz.z * 0.5 + 0.015), wood)
	var cols := int(sz.x / 0.12)
	var rows := int(sz.y / 0.12)
	for i in cols + 1:
		b(root, Vector3(0.015, sz.y, sz.z), Vector3(-sz.x * 0.5 + i * sz.x / float(cols), sz.y * 0.5, 0), wood)
	for j in rows + 1:
		b(root, Vector3(sz.x, 0.015, sz.z), Vector3(0, j * sz.y / float(rows), 0), wood)
	for i in cols:
		for j in rows:
			if rng.randf() < 0.75:
				var p := Vector3(-sz.x * 0.5 + (i + 0.5) * sz.x / float(cols), (j + 0.5) * sz.y / float(rows), 0.0)
				c(root, 0.037, sz.z * 0.9, p, mat("bottle", Color(0.06, 0.12, 0.06), 0.1, 0.0), Vector3(0, 0, 1), 10)


static func _bollard(root: Node3D, sz: Vector3) -> void:
	var r := minf(sz.x, sz.z) * 0.35
	c(root, r, sz.y - 0.05, Vector3(0, (sz.y - 0.05) * 0.5, 0), mat("bollard_yellow", Color(0.85, 0.65, 0.05), 0.5, 0.2))
	c(root, r * 1.05, 0.05, Vector3(0, sz.y - 0.025, 0), mat("steel_dark", Color(0.12, 0.12, 0.12), 0.5, 0.6))
	for k in 2:
		c(root, r * 1.02, 0.06, Vector3(0, sz.y * (0.55 + k * 0.15), 0), mat("bollard_band", Color(0.05, 0.05, 0.05), 0.5))
	c(root, r * 1.6, 0.03, Vector3(0, 0.015, 0), mat("steel_dark", Color(0.12, 0.12, 0.12), 0.5, 0.6))


static func _planter(root: Node3D, sz: Vector3, rng: RandomNumberGenerator) -> void:
	var conc := world(TfpsMaterials.CONCRETE, Color(1.1, 1.08, 1.04))
	var wall := 0.08
	b(root, Vector3(sz.x, sz.y, wall), Vector3(0, sz.y * 0.5, sz.z * 0.5 - wall * 0.5), conc)
	b(root, Vector3(sz.x, sz.y, wall), Vector3(0, sz.y * 0.5, -sz.z * 0.5 + wall * 0.5), conc)
	b(root, Vector3(wall, sz.y, sz.z), Vector3(sz.x * 0.5 - wall * 0.5, sz.y * 0.5, 0), conc)
	b(root, Vector3(wall, sz.y, sz.z), Vector3(-sz.x * 0.5 + wall * 0.5, sz.y * 0.5, 0), conc)
	b(root, Vector3(sz.x - wall * 2.0, 0.05, sz.z - wall * 2.0), Vector3(0, sz.y - 0.08, 0), mat("soil", Color(0.12, 0.08, 0.05), 1.0))
	var n := maxi(2, int(sz.x * sz.z * 2.0))
	for i in n:
		var r := rng.randf_range(0.25, 0.45)
		_plant(root, Vector3(rng.randf_range(-0.35, 0.35) * sz.x, sz.y + r * 0.6, rng.randf_range(-0.3, 0.3) * sz.z), Vector3(r, r * 0.8, r), rng)


## A trimmed hedge: one rounded, leafy block (leaves.gdshader) the size of
## the solid, with a woody base showing under it.
static func _hedge(root: Node3D, sz: Vector3, rng: RandomNumberGenerator) -> void:
	var box := BoxMesh.new()
	var size := sz * Vector3(1.0, 0.97, 1.0)
	box.size = size
	box.subdivide_width = clampi(int(size.x / 0.06), 8, 120)
	box.subdivide_height = clampi(int(size.y / 0.06), 8, 60)
	box.subdivide_depth = clampi(int(size.z / 0.06), 8, 60)
	var mi := MeshInstance3D.new()
	mi.mesh = box
	mi.material_override = leaves()
	mi.position = Vector3(0, size.y * 0.5 + 0.03, 0)
	mi.set_instance_shader_parameter("half_size", size * 0.5)
	mi.set_instance_shader_parameter("corner", minf(0.22, minf(size.y, size.z) * 0.35))
	mi.set_instance_shader_parameter("tint", Color(1, 1, 1).lerp(Color(1.15, 1.05, 0.8), rng.randf()))
	root.add_child(mi)
	b(root, Vector3(sz.x * 0.9, 0.12, sz.z * 0.6), Vector3(0, 0.06, 0), mat("hedge_wood", Color(0.09, 0.07, 0.05), 0.9))


## A shrub: a few overlapping leafy masses.
static func _plant(root: Node3D, centre: Vector3, radii: Vector3, rng: RandomNumberGenerator) -> void:
	var tint := Color(1, 1, 1).lerp(Color(1.2, 1.1, 0.75), rng.randf())
	_leaf_mass(root, radii, centre, tint)
	for k in 2:
		var o := Vector3(rng.randf_range(-0.5, 0.5) * radii.x, rng.randf_range(-0.1, 0.4) * radii.y, rng.randf_range(-0.5, 0.5) * radii.z)
		_leaf_mass(root, radii * rng.randf_range(0.55, 0.75), centre + o, tint)


static func _leaf_mass(root: Node3D, radii: Vector3, centre: Vector3, tint: Color) -> void:
	if _leaf_sph == null:
		_leaf_sph = SphereMesh.new()
		_leaf_sph.radius = 0.5
		_leaf_sph.height = 1.0
		_leaf_sph.radial_segments = 40
		_leaf_sph.rings = 20
	var mi := MeshInstance3D.new()
	mi.mesh = _leaf_sph
	mi.scale = radii * 2.0
	mi.position = centre
	mi.material_override = leaves()
	mi.set_instance_shader_parameter("tint", tint)
	root.add_child(mi)


static func leaves() -> Material:
	if not _mats.has("leaves"):
		var m := ShaderMaterial.new()
		m.shader = load("res://shaders/leaves.gdshader")
		_mats["leaves"] = m
	return _mats["leaves"]


static func _column(root: Node3D, sz: Vector3) -> void:
	var conc := world(TfpsMaterials.CONCRETE, Color(1.15, 1.13, 1.08))
	var r := minf(sz.x, sz.z) * 0.5
	c(root, r * 0.86, sz.y - 0.5, Vector3(0, sz.y * 0.5, 0), conc, Vector3.UP, 28)
	b(root, Vector3(r * 2.0, 0.25, r * 2.0), Vector3(0, 0.125, 0), conc)
	b(root, Vector3(r * 2.0, 0.25, r * 2.0), Vector3(0, sz.y - 0.125, 0), conc)


static func _car(root: Node3D, sz: Vector3, rng: RandomNumberGenerator) -> void:
	var paints := [Color(0.08, 0.08, 0.09), Color(0.6, 0.6, 0.62), Color(0.35, 0.05, 0.05), Color(0.08, 0.15, 0.3), Color(0.85, 0.85, 0.83)]
	var paint := mat("car_paint_%d" % (rng.randi() % paints.size()), paints[rng.randi() % paints.size()], 0.25, 0.6)
	var clear := sz.y * 0.28
	b(root, Vector3(sz.x - 0.1, sz.y - clear, sz.z), Vector3(0, clear + (sz.y - clear) * 0.5, 0), paint)
	b(root, Vector3(0.12, (sz.y - clear) * 0.7, sz.z - 0.08), Vector3(sz.x * 0.5 - 0.06, clear + (sz.y - clear) * 0.4, 0), paint)
	b(root, Vector3(0.12, (sz.y - clear) * 0.7, sz.z - 0.08), Vector3(-sz.x * 0.5 + 0.06, clear + (sz.y - clear) * 0.4, 0), paint)
	var rubber := mat("tyre", Color(0.03, 0.03, 0.03), 0.9)
	var rim := mat("rim", Color(0.6, 0.6, 0.62), 0.3, 1.0)
	for sx in [-1.0, 1.0]:
		for zz in [-1.0, 1.0]:
			var p := Vector3(sx * (sz.x * 0.5 - 0.7), 0.32, zz * (sz.z * 0.5 - 0.1))
			c(root, 0.32, 0.22, p, rubber, Vector3(0, 0, 1), 20)
			c(root, 0.2, 0.23, p, rim, Vector3(0, 0, 1), 16)
	for zz in [-1.0, 1.0]:
		b(root, Vector3(0.05, 0.08, 0.25), Vector3(sz.x * 0.5 - 0.02, sz.y * 0.75, zz * (sz.z * 0.5 - 0.2)), mat("headlight", Color(0.9, 0.9, 0.85), 0.1, 0.0, Color(0.3, 0.3, 0.25)))
		b(root, Vector3(0.05, 0.08, 0.3), Vector3(-sz.x * 0.5 + 0.02, sz.y * 0.75, zz * (sz.z * 0.5 - 0.2)), mat("taillight", Color(0.6, 0.02, 0.02), 0.2, 0.0, Color(0.2, 0.0, 0.0)))
	b(root, Vector3(0.06, 0.14, sz.z * 0.5), Vector3(sz.x * 0.5 - 0.01, sz.y * 0.5, 0), mat("grille", Color(0.05, 0.05, 0.05), 0.5))


static func _car_cabin(root: Node3D, sz: Vector3) -> void:
	var glass := mat("car_glass", Color(0.05, 0.07, 0.09), 0.05, 0.3)
	b(root, Vector3(sz.x * 0.94, sz.y * 0.92, sz.z * 0.94), Vector3(0, sz.y * 0.46, 0), glass)
	var trim := mat("car_trim", Color(0.04, 0.04, 0.04), 0.5)
	b(root, Vector3(sz.x, 0.06, sz.z), Vector3(0, sz.y - 0.03, 0), trim)
	for sx in [-0.5, 0.0, 0.5]:
		for zz in [-1.0, 1.0]:
			b(root, Vector3(0.07, sz.y, 0.04), Vector3(sx * (sz.x - 0.07), sz.y * 0.5, zz * (sz.z * 0.5 - 0.02)), trim)


static func _dumpster(root: Node3D, sz: Vector3) -> void:
	var green := mat("dumpster", Color(0.08, 0.25, 0.12), 0.6, 0.3)
	b(root, Vector3(sz.x, sz.y - 0.2, sz.z), Vector3(0, 0.15 + (sz.y - 0.2) * 0.5, 0), green)
	var lid := b(root, Vector3(sz.x + 0.04, 0.05, sz.z + 0.06), Vector3(0, sz.y - 0.02, 0), mat("dumpster_lid", Color(0.05, 0.05, 0.05), 0.7))
	lid.rotation.z = 0.06
	for k in 3:
		b(root, Vector3(0.04, sz.y - 0.25, sz.z + 0.02), Vector3(-sz.x * 0.3 + k * sz.x * 0.3, 0.15 + (sz.y - 0.25) * 0.5, 0), green)
	for sx in [-1.0, 1.0]:
		for zz in [-1.0, 1.0]:
			c(root, 0.07, 0.05, Vector3(sx * (sz.x * 0.5 - 0.12), 0.07, zz * (sz.z * 0.5 - 0.12)), mat("tyre", Color(0.03, 0.03, 0.03), 0.9), Vector3(0, 0, 1), 12)


static func _railing(root: Node3D, sz: Vector3, tag: String) -> void:
	var stone := tag == "balustrade"
	var m = world(TfpsMaterials.CONCRETE, Color(1.2, 1.18, 1.12)) if stone else mat("rail_steel", Color(0.18, 0.19, 0.2), 0.4, 0.8)
	if stone:
		b(root, Vector3(sz.x, 0.12, sz.z + 0.08), Vector3(0, sz.y - 0.06, 0), m)
		b(root, Vector3(sz.x, 0.1, sz.z + 0.08), Vector3(0, 0.05, 0), m)
		var n := int(sz.x / 0.22)
		for i in n:
			c(root, 0.045, sz.y - 0.22, Vector3(-sz.x * 0.5 + (i + 0.5) * sz.x / float(n), sz.y * 0.5, 0), m, Vector3.UP, 10)
		return
	var posts := maxi(2, int(sz.x / 1.2) + 1)
	for i in posts:
		var x := -sz.x * 0.5 + 0.03 + i * (sz.x - 0.06) / float(posts - 1)
		b(root, Vector3(0.04, sz.y, 0.04), Vector3(x, sz.y * 0.5, 0), m)
	c(root, 0.025, sz.x, Vector3(0, sz.y - 0.025, 0), m, Vector3.RIGHT, 12)
	for y in [0.35, 0.65]:
		c(root, 0.012, sz.x, Vector3(0, sz.y * y, 0), m, Vector3.RIGHT, 8)
	b(root, Vector3(sz.x, 0.1, 0.01), Vector3(0, 0.05, 0), m)


static func _fountain(root: Node3D, sz: Vector3) -> void:
	var stone := world(TfpsMaterials.CONCRETE, Color(1.25, 1.2, 1.12))
	var wall := 0.2
	b(root, Vector3(sz.x, sz.y, wall), Vector3(0, sz.y * 0.5, sz.z * 0.5 - wall * 0.5), stone)
	b(root, Vector3(sz.x, sz.y, wall), Vector3(0, sz.y * 0.5, -sz.z * 0.5 + wall * 0.5), stone)
	b(root, Vector3(wall, sz.y, sz.z), Vector3(sz.x * 0.5 - wall * 0.5, sz.y * 0.5, 0), stone)
	b(root, Vector3(wall, sz.y, sz.z), Vector3(-sz.x * 0.5 + wall * 0.5, sz.y * 0.5, 0), stone)
	b(root, Vector3(sz.x - wall * 2.0, 0.02, sz.z - wall * 2.0), Vector3(0, sz.y - 0.12, 0), mat("water", Color(0.05, 0.1, 0.1), 0.02, 0.3))
	c(root, 0.25, sz.y + 0.6, Vector3(0, (sz.y + 0.6) * 0.5, 0), stone, Vector3.UP, 20)
	c(root, 0.6, 0.12, Vector3(0, sz.y + 0.6, 0), stone, Vector3.UP, 24)
