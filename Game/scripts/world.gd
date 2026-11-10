## Builds the visible map from a map definition (the same JSON the server
## uses): boxes with procedural materials, lighting and atmosphere.
class_name TfpsWorld
extends Node3D

## Graphics quality: 0 low, 1 medium, 2 high, 3 ultra.
static var quality := 2

var map_name := ""
var solid_nodes := {}          # solid index -> MeshInstance3D (intact solids)
var props := {}                # solid index -> prop drawn instead of the box (Blender asset)
var broken := {}               # solid index -> MultiMeshInstance3D (cells)
var cell_slot := {}            # cell id -> [solid index, instance index]
var net: TfpsNet
var _box_mesh := BoxMesh.new()
var _solids: Array = []
var dressing := {}             # solid index -> [nodes] drawn over it (hidden when it breaks)
var doors := {}                # door solid index -> {node, closed, hinge, box, open, t}
var _container_tints := {}


## Builds the map from the native world description (Godot space, metres).
func build(world: Dictionary, p_net: TfpsNet) -> void:
	net = p_net
	map_name = net.map_name if net else ""
	for c in get_children():
		c.queue_free()
	solid_nodes.clear()
	props.clear()
	_chipped.clear()
	_rubble.clear()
	doors.clear()
	broken.clear()
	cell_slot.clear()
	_solids = world.get("solids", [])
	_container_tints = TfpsDressing.container_tints(_solids)
	_build_environment()
	var ceilings: Array[AABB] = []
	for s in _solids:
		_add_solid(s)
		var box := _aabb(s)
		if box.size.y < 0.6 and box.position.y > 2.4 and box.size.x * box.size.z > 12.0 and s.get("tag", "") != "ground":
			ceilings.append(box)
	_build_interior_lights(ceilings)
	dressing = TfpsDressing.build(self, _solids)
	_build_surroundings()


## Ground beyond the playable area, out to the horizon (just below the map's ground).
func _build_surroundings() -> void:
	var lo := Vector3.INF
	for s in _solids:
		if s.get("tag", "") == "ground":
			lo = TfpsNet.vec(s["max"])
	var y := (lo.y if lo != Vector3.INF else 0.0) - 0.02
	var mi := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(1600, 1600)
	mi.mesh = pm
	mi.position = Vector3(0, y, 0)
	mi.material_override = TfpsMaterials.get_material(TfpsMaterials.DIRT)
	mi.set_instance_shader_parameter("tint", Color(0.78, 0.8, 0.7))
	mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(mi)
	_build_backdrop(y)


## Distant scenery beyond the map (drawn only): building silhouettes all
## round, gantry cranes at the harbour, tree belts elsewhere. Fog gives it
## aerial perspective.
func _build_backdrop(ground_y: float) -> void:
	var rng := RandomNumberGenerator.new()
	rng.seed = hash(map_name) if map_name != "" else 7
	var extent := 0.0
	for s in _solids:
		var b := _aabb(s)
		extent = maxf(extent, maxf(absf(b.position.x), absf(b.end.x)))
		extent = maxf(extent, maxf(absf(b.position.z), absf(b.end.z)))
	var harbour := map_name.contains("harbor")
	var mm_box := MultiMesh.new()
	mm_box.transform_format = MultiMesh.TRANSFORM_3D
	mm_box.use_colors = true
	var boxes: Array = []
	for i in 120:
		var a := rng.randf() * TAU
		var r := rng.randf_range(extent + 170.0, extent + 700.0)
		var w := rng.randf_range(14.0, 55.0)
		var d := rng.randf_range(14.0, 45.0)
		var h := rng.randf_range(7.0, 24.0) if rng.randf() < 0.85 else rng.randf_range(28.0, 60.0)
		var p := Vector3(cos(a) * r, ground_y + h * 0.5, sin(a) * r)
		var shade := rng.randf_range(0.2, 0.36)
		var col := Color(shade, shade * rng.randf_range(0.97, 1.03), shade * rng.randf_range(0.98, 1.08))
		boxes.append([Transform3D(Basis.from_euler(Vector3(0, rng.randf() * TAU, 0)).scaled(Vector3(w, h, d)), p), col])
	if harbour:
		for i in 4:
			var a := -0.4 + i * 0.25
			var r := extent + 120.0 + i * 15.0
			var base := Vector3(cos(a) * r, ground_y, sin(a) * r)
			var yaw := Basis.from_euler(Vector3(0, a, 0))
			var steel := Color(0.55, 0.22, 0.12) if i % 2 == 0 else Color(0.2, 0.32, 0.45)
			for leg in [Vector3(-8, 0, -6), Vector3(8, 0, -6), Vector3(-8, 0, 6), Vector3(8, 0, 6)]:
				boxes.append([Transform3D(Basis().scaled(Vector3(1.2, 38, 1.2)), base + yaw * (leg + Vector3(0, 19, 0))), steel])
			boxes.append([Transform3D(yaw.scaled(Vector3(2.5, 2.5, 70)), base + yaw * Vector3(0, 40, 10)), steel])
			boxes.append([Transform3D(yaw.scaled(Vector3(18, 2, 2)), base + yaw * Vector3(0, 36, -6)), steel])
			boxes.append([Transform3D(yaw.scaled(Vector3(6, 4, 6)), base + yaw * Vector3(0, 42, 0)), Color(0.75, 0.75, 0.72)])
	mm_box.mesh = BoxMesh.new()
	mm_box.instance_count = boxes.size()
	for i in boxes.size():
		mm_box.set_instance_transform(i, boxes[i][0])
		mm_box.set_instance_color(i, boxes[i][1])
	var mmi := MultiMeshInstance3D.new()
	mmi.multimesh = mm_box
	var m := ShaderMaterial.new()
	m.shader = load("res://shaders/backdrop.gdshader")
	mmi.material_override = m
	mmi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(mmi)
	if not harbour:
		var trees := MultiMesh.new()
		trees.transform_format = MultiMesh.TRANSFORM_3D
		trees.use_colors = true
		var sphere := SphereMesh.new()
		sphere.radial_segments = 10
		sphere.rings = 6
		trees.mesh = sphere
		trees.instance_count = 420
		for i in 420:
			var a := rng.randf() * TAU
			var r := rng.randf_range(extent + 35.0, extent + 160.0)
			var s := rng.randf_range(4.0, 9.0)
			var p := Vector3(cos(a) * r, ground_y + s * 0.7, sin(a) * r)
			trees.set_instance_transform(i, Transform3D(Basis().scaled(Vector3(s, s * 1.15, s)), p))
			var g := rng.randf_range(0.07, 0.13)
			trees.set_instance_color(i, Color(g * 0.85, g * 1.15, g * 0.6))
		var tm := MultiMeshInstance3D.new()
		tm.multimesh = trees
		var leaves := ShaderMaterial.new()
		leaves.shader = load("res://shaders/backdrop.gdshader")
		leaves.set_shader_parameter("foliage", true)
		tm.material_override = leaves
		tm.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		add_child(tm)


static func _aabb(s: Dictionary) -> AABB:
	var a := TfpsNet.vec(s["min"])
	var b := TfpsNet.vec(s["max"])
	return AABB(a, b - a).abs()


func _add_solid(s: Dictionary) -> void:
	var box := _aabb(s)
	var mat := int(s.get("material", 0))
	var tag: String = s.get("tag", "")
	var mi := MeshInstance3D.new()
	mi.position = box.get_center()
	if tag == "container" and minf(box.size.x, minf(box.size.y, box.size.z)) < 0.2:
		mi.mesh = TfpsDressing.container_panel(box.size)   # corrugated sheet
	else:
		mi.mesh = _box_mesh
		mi.scale = box.size
	mi.material_override = TfpsMaterials.get_material(mat)
	_style(mi, mat, tag, box)
	if _container_tints.has(int(s["index"])):
		mi.set_instance_shader_parameter("tint", _container_tints[int(s["index"])])
	add_child(mi)
	solid_nodes[int(s["index"])] = mi
	# Dress it with a Blender asset when one matches (the box stays for breaking).
	var prop := TfpsProps.for_solid(tag, box)
	if prop == null:
		prop = TfpsFurniture.model(tag, box)
	if prop != null:
		add_child(prop)
		props[int(s["index"])] = prop
		mi.visible = false
	if tag == "door":
		var node: Node3D = prop if prop != null else mi
		var along_x := box.size.x >= box.size.z
		var hinge := Vector3(box.position.x, box.position.y, box.get_center().z) if along_x else Vector3(box.get_center().x, box.position.y, box.position.z)
		doors[int(s["index"])] = {"node": node, "closed": node.transform, "hinge": hinge, "box": box, "open": false, "t": 0.0}
	elif mat == TfpsMaterials.GLASS and tag == "window_glass":
		var frame := TfpsProps.window_frame(box)
		if frame != null:
			add_child(frame)


func _style(gi: GeometryInstance3D, mat: int, tag: String, box: AABB) -> void:
	if mat == TfpsMaterials.GLASS:
		gi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	elif mat == TfpsMaterials.DRYWALL:
		gi.set_instance_shader_parameter("tint", TfpsDressing.room_paint(box.get_center()))
	else:
		gi.set_instance_shader_parameter("tint", TfpsMaterials.tint_for(mat, tag, box.position))
		gi.set_instance_shader_parameter("seed", float(absi(hash(Vector3i(box.position.round()))) % 997) * 0.37)


## A new solid placed during the match (barricade).
func add_runtime_solids(world: Dictionary, indices: Array) -> void:
	_solids = world.get("solids", [])
	for i in indices:
		if i < _solids.size():
			_add_solid(_solids[i])


## Cells destroyed this frame: a solid switches to per-cell drawing the
## first time one of its cells breaks.
func destroy_cells(ids: Array) -> Array:
	var info := []
	var unknown := []
	for id in ids:
		if not cell_slot.has(int(id)):
			unknown.append(int(id))
	if not unknown.is_empty():
		for c in net.cells(unknown):
			var solid := int(c["solid"])
			if not broken.has(solid):
				_break_solid(solid)
	for id in ids:
		var slot = cell_slot.get(int(id))
		if slot == null:
			continue
		var mm: MultiMesh = broken[slot[0]].multimesh
		var t := mm.get_instance_transform(slot[1])
		var mat := int(_solids[slot[0]]["material"])
		info.append({"center": t.origin, "size": t.basis.get_scale(), "material": mat})
		mm.set_instance_transform(slot[1], Transform3D(Basis.from_scale(Vector3.ZERO), t.origin))
		cell_slot.erase(int(id))
		if mat != TfpsMaterials.GLASS:
			_chip_neighbours(mm, t, int(id))
			_drop_rubble(int(slot[0]), t, mat, int(id))
	return info


## Cells around a fresh hole are chipped: shrunk a little across the wall
## face, tilted and nudged, so breaches have ragged edges instead of a
## clean grid (visual only; collision is the server's cells).
func _chip_neighbours(mm: MultiMesh, hole: Transform3D, seed_id: int) -> void:
	var size := hole.basis.get_scale()
	var reach := maxf(size.x, maxf(size.y, size.z)) * 1.1
	var rng := RandomNumberGenerator.new()
	for i in mm.instance_count:
		var t := mm.get_instance_transform(i)
		var sc := t.basis.get_scale()
		if sc.x == 0.0 or _chipped.has(mm.get_instance_id() * 100000 + i):
			continue
		if t.origin.distance_to(hole.origin) > reach:
			continue
		_chipped[mm.get_instance_id() * 100000 + i] = true
		rng.seed = seed_id * 7919 + i
		# Chips stay in the wall's plane: shrink across the face, turn about
		# the wall normal (the cell's thinnest axis) only.
		var thin := 0 if sc.x <= sc.y and sc.x <= sc.z else (2 if sc.z <= sc.y else 1)
		var k := Vector3(rng.randf_range(0.72, 0.94), rng.randf_range(0.72, 0.94), rng.randf_range(0.72, 0.94))
		k[thin] = 1.0
		var axis := Vector3.ZERO
		axis[thin] = 1.0
		var tilt := Basis(axis, rng.randf_range(-0.22, 0.22))
		var nudge := (hole.origin - t.origin) * rng.randf_range(-0.06, 0.04)
		nudge[thin] = 0.0
		mm.set_instance_transform(i, Transform3D(tilt * Basis.from_scale(sc * k), t.origin + nudge))


## Irregular chunks of the wall's material settle on the floor below a
## broken cell and stay there (at most MAX_RUBBLE in the map).
const MAX_RUBBLE := 500
var _chipped := {}
var _rubble: Array[Node3D] = []
static var _chunk_meshes: Array[Mesh] = []


static func _chunk_mesh(variant: int) -> Mesh:
	if _chunk_meshes.is_empty():
		for v in 6:
			var rng := RandomNumberGenerator.new()
			rng.seed = 1000 + v
			var st := SurfaceTool.new()
			st.begin(Mesh.PRIMITIVE_TRIANGLES)
			# A box with every corner pulled in or out: a broken lump.
			var c: Array[Vector3] = []
			for corner in 8:
				var p := Vector3(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1) - Vector3(0.5, 0.5, 0.5)
				c.append(p * Vector3(rng.randf_range(0.6, 1.3), rng.randf_range(0.5, 1.1), rng.randf_range(0.6, 1.3)))
			for f in [[0, 1, 3, 2], [4, 6, 7, 5], [0, 4, 5, 1], [2, 3, 7, 6], [0, 2, 6, 4], [1, 5, 7, 3]]:
				var a: Vector3 = c[f[0]]
				var b: Vector3 = c[f[1]]
				var d: Vector3 = c[f[2]]
				var e: Vector3 = c[f[3]]
				var n := (b - a).cross(e - a).normalized()
				for tri in [[a, b, d], [a, d, e]]:
					var tn: Vector3 = (tri[1] - tri[0]).cross(tri[2] - tri[0])
					var pts: Array = tri if tn.dot(n) >= 0.0 else [tri[0], tri[2], tri[1]]
					for q in pts:
						st.set_normal(n)
						st.add_vertex(q)
			st.generate_normals()
			_chunk_meshes.append(st.commit())
	return _chunk_meshes[variant % _chunk_meshes.size()]


func _drop_rubble(solid_index: int, cell: Transform3D, mat: int, seed_id: int) -> void:
	var box := _aabb(_solids[solid_index])
	var size := cell.basis.get_scale()
	var rng := RandomNumberGenerator.new()
	rng.seed = seed_id * 31 + 7
	var floor_y := box.position.y
	for k in rng.randi_range(1, 3):
		var mi := MeshInstance3D.new()
		mi.mesh = _chunk_mesh(rng.randi())
		var s := maxf(size.x, maxf(size.y, size.z)) * rng.randf_range(0.25, 0.55)
		mi.scale = Vector3.ONE * s
		mi.rotation = Vector3(rng.randf() * TAU, rng.randf() * TAU, rng.randf() * TAU)
		mi.position = Vector3(cell.origin.x + rng.randf_range(-0.6, 0.6), floor_y + s * 0.3, cell.origin.z + rng.randf_range(-0.6, 0.6))
		mi.material_override = TfpsMaterials.get_material(mat)
		mi.set_instance_shader_parameter("tint", TfpsMaterials.tint_for(mat, "", box.position).darkened(0.1))
		add_child(mi)
		_rubble.append(mi)
		if _rubble.size() > MAX_RUBBLE:
			_rubble.pop_front().queue_free()


## A door opened or closed (server state): it swings 95 degrees about its
## hinge over a fraction of a second (visual only).
func set_door(index: int, open: bool) -> void:
	if not doors.has(index):
		return
	doors[index]["open"] = open


func _process(delta: float) -> void:
	for index in doors:
		var d: Dictionary = doors[index]
		var goal := 1.0 if d["open"] else 0.0
		if absf(float(d["t"]) - goal) < 0.0001:
			continue
		d["t"] = move_toward(float(d["t"]), goal, delta * 2.4)
		var node: Node3D = d["node"]
		if not is_instance_valid(node):
			continue
		var k := float(d["t"])
		var ease := k * k * (3.0 - 2.0 * k)
		var h: Vector3 = d["hinge"]
		var swing := Transform3D(Basis(Vector3.UP, deg_to_rad(-95.0) * ease), Vector3.ZERO)
		node.transform = Transform3D(Basis(), h) * swing * Transform3D(Basis(), -h) * (d["closed"] as Transform3D)


## The door the view ray hits within `reach` metres (index), or -1.
func door_in_view(origin: Vector3, dir: Vector3, reach: float) -> int:
	var best := -1
	var best_t := reach
	for index in doors:
		var d: Dictionary = doors[index]
		var box: AABB = d["box"]
		if float(d["t"]) > 0.5:
			# Open: the leaf stands beside the hinge, perpendicular to the frame.
			var node: Node3D = d["node"]
			box = node.global_transform * AABB(Vector3(-0.5, -0.5, -0.5), Vector3.ONE) if node is MeshInstance3D else box
		var hit = box.intersects_ray(origin, dir)
		if hit != null:
			var t := origin.distance_to(hit)
			if t < best_t:
				best_t = t
				best = int(index)
	return best


func _break_solid(index: int) -> void:
	var s: Dictionary = _solids[index]
	if not s.get("destructible", false):
		return
	var first := int(s["first_cell"])
	var ids := range(first, first + int(s["cell_count"]))
	var cells := net.cells(ids)
	var mm := MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	mm.mesh = _box_mesh
	mm.instance_count = cells.size()
	for i in cells.size():
		var c: Dictionary = cells[i]
		var a := TfpsNet.vec(c["min"])
		var b := TfpsNet.vec(c["max"])
		var size := (b - a).abs()
		var xf := Transform3D(Basis.from_scale(size if not c.get("destroyed", false) else Vector3.ZERO), (a + b) * 0.5)
		mm.set_instance_transform(i, xf)
		if not c.get("destroyed", false):
			cell_slot[int(c["id"])] = [index, i]
	var mmi := MultiMeshInstance3D.new()
	mmi.multimesh = mm
	var mat := int(s["material"])
	mmi.material_override = TfpsMaterials.get_material(mat)
	_style(mmi, mat, s.get("tag", ""), _aabb(s))
	if _container_tints.has(index):
		mmi.set_instance_shader_parameter("tint", _container_tints[index])
	add_child(mmi)
	broken[index] = mmi
	for n in dressing.get(index, []):
		(n as Node3D).visible = false
	doors.erase(index)
	if solid_nodes.has(index):
		solid_nodes[index].queue_free()
		solid_nodes.erase(index)
	if props.has(index):
		props[index].queue_free()
		props.erase(index)


## A wall panel was reinforced: its surface becomes steel plate.
func reinforce(indices: Array) -> void:
	for i in indices:
		var node: GeometryInstance3D = solid_nodes.get(i, broken.get(i))
		if node != null:
			node.material_override = TfpsMaterials.get_material(TfpsMaterials.REINFORCED)
			node.set_instance_shader_parameter("tint", Color(1, 1, 1))


func _build_environment() -> void:
	var we := WorldEnvironment.new()
	var env := Environment.new()
	var sky := Sky.new()
	var sky_mat := ShaderMaterial.new()
	sky_mat.shader = load("res://shaders/sky.gdshader")
	sky.sky_material = sky_mat
	env.background_mode = Environment.BG_SKY
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.ambient_light_energy = 0.85
	env.tonemap_exposure = 1.0
	env.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	env.tonemap_mode = Environment.TONE_MAPPER_AGX
	env.tonemap_white = 6.0
	env.ssao_enabled = quality >= 1
	env.ssao_radius = 1.2
	env.ssao_intensity = 2.0
	env.ssil_enabled = quality >= 2
	env.ssr_enabled = quality >= 2
	env.ssr_max_steps = 48
	env.ssr_fade_in = 0.2
	env.ssr_fade_out = 2.0
	env.sdfgi_enabled = quality >= 3
	env.sdfgi_use_occlusion = true
	env.sdfgi_cascades = 4
	env.sdfgi_min_cell_size = 0.2
	env.glow_enabled = true
	env.glow_intensity = 0.6
	env.glow_bloom = 0.05
	env.volumetric_fog_enabled = quality >= 1
	env.volumetric_fog_density = 0.005
	env.volumetric_fog_albedo = Color(0.9, 0.88, 0.85)
	env.volumetric_fog_anisotropy = 0.6
	env.volumetric_fog_length = 64.0
	env.fog_enabled = true
	env.fog_mode = Environment.FOG_MODE_EXPONENTIAL
	env.fog_density = 0.0028
	env.fog_aerial_perspective = 0.7
	env.fog_sky_affect = 0.0
	env.fog_light_color = Color(0.68, 0.74, 0.82)
	env.adjustment_enabled = true
	env.adjustment_contrast = 1.14
	env.adjustment_saturation = 1.04
	we.environment = env
	add_child(we)

	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-38.0, -125.0, 0.0)
	sun.light_color = Color(1.0, 0.93, 0.82)
	sun.light_energy = 2.7
	sun.shadow_enabled = true
	if quality == 0:
		sun.directional_shadow_mode = DirectionalLight3D.SHADOW_ORTHOGONAL
		sun.directional_shadow_max_distance = 50.0
	else:
		sun.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
		sun.directional_shadow_max_distance = 120.0
	sun.light_volumetric_fog_energy = 1.5
	add_child(sun)


func _build_interior_lights(ceilings: Array[AABB]) -> void:
	# Warm fittings on a 6 m grid under each ceiling, just below it.
	var placed := 0
	for c in ceilings:
		var nx := maxi(1, int(c.size.x / 6.0))
		var nz := maxi(1, int(c.size.z / 6.0))
		for ix in nx:
			for iz in nz:
				if placed >= 48:
					return
				var p := Vector3(c.position.x + (ix + 0.5) * c.size.x / nx, c.position.y - 0.25,
					c.position.z + (iz + 0.5) * c.size.z / nz)
				var l := OmniLight3D.new()
				l.position = p
				l.omni_range = 11.0
				l.light_energy = 2.6
				l.light_color = Color(1.0, 0.86, 0.68)
				l.shadow_enabled = placed < (16 if quality >= 2 else 0)
				l.light_volumetric_fog_energy = 0.5
				add_child(l)
				var fitting := MeshInstance3D.new()
				var fm := BoxMesh.new()
				fm.size = Vector3(1.2, 0.06, 0.25)
				fitting.mesh = fm
				var em := StandardMaterial3D.new()
				em.emission_enabled = true
				em.emission = Color(1.0, 0.9, 0.75)
				em.emission_energy_multiplier = 4.0
				em.albedo_color = Color(1, 1, 1)
				fitting.material_override = em
				fitting.position = p + Vector3(0, 0.2, 0)
				fitting.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
				add_child(fitting)
				placed += 1
