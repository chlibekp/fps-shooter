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


## Builds the map from the native world description (Godot space, metres).
func build(world: Dictionary, p_net: TfpsNet) -> void:
	net = p_net
	for c in get_children():
		c.queue_free()
	solid_nodes.clear()
	props.clear()
	broken.clear()
	cell_slot.clear()
	_solids = world.get("solids", [])
	_build_environment()
	var ceilings: Array[AABB] = []
	for s in _solids:
		_add_solid(s)
		var box := _aabb(s)
		if box.size.y < 0.6 and box.position.y > 2.4 and box.size.x * box.size.z > 12.0 and s.get("tag", "") != "ground":
			ceilings.append(box)
	_build_interior_lights(ceilings)
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


static func _aabb(s: Dictionary) -> AABB:
	var a := TfpsNet.vec(s["min"])
	var b := TfpsNet.vec(s["max"])
	return AABB(a, b - a).abs()


func _add_solid(s: Dictionary) -> void:
	var box := _aabb(s)
	var mat := int(s.get("material", 0))
	var tag: String = s.get("tag", "")
	var mi := MeshInstance3D.new()
	mi.mesh = _box_mesh
	mi.position = box.get_center()
	mi.scale = box.size
	mi.material_override = TfpsMaterials.get_material(mat)
	_style(mi, mat, tag, box)
	add_child(mi)
	solid_nodes[int(s["index"])] = mi
	# Dress it with a Blender asset when one matches (the box stays for breaking).
	var prop := TfpsProps.for_solid(tag, box)
	if prop != null:
		add_child(prop)
		props[int(s["index"])] = prop
		mi.visible = false
	elif mat == TfpsMaterials.GLASS and tag == "window_glass":
		var frame := TfpsProps.window_frame(box)
		if frame != null:
			add_child(frame)


func _style(gi: GeometryInstance3D, mat: int, tag: String, box: AABB) -> void:
	if mat == TfpsMaterials.GLASS:
		gi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
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
		info.append({"center": t.origin, "size": t.basis.get_scale(), "material": int(_solids[slot[0]]["material"])})
		mm.set_instance_transform(slot[1], Transform3D(Basis.from_scale(Vector3.ZERO), t.origin))
		cell_slot.erase(int(id))
	return info


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
	add_child(mmi)
	broken[index] = mmi
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
	env.ambient_light_energy = 1.3
	env.tonemap_exposure = 1.15
	env.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	env.tonemap_mode = Environment.TONE_MAPPER_AGX
	env.tonemap_white = 6.0
	env.ssao_enabled = quality >= 1
	env.ssao_radius = 1.2
	env.ssao_intensity = 2.0
	env.ssil_enabled = quality >= 2
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
	env.adjustment_enabled = true
	env.adjustment_contrast = 1.08
	env.adjustment_saturation = 1.05
	we.environment = env
	add_child(we)

	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-38.0, -125.0, 0.0)
	sun.light_color = Color(1.0, 0.93, 0.82)
	sun.light_energy = 2.2
	sun.shadow_enabled = true
	if quality == 0:
		sun.directional_shadow_mode = DirectionalLight3D.SHADOW_ORTHOGONAL
		sun.directional_shadow_max_distance = 50.0
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
