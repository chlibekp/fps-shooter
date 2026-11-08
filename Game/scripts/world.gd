## Builds the visible map from a map definition (the same JSON the server
## uses): boxes with procedural materials, lighting and atmosphere.
class_name TfpsWorld
extends Node3D

var map_name := ""
var solid_nodes: Array[MeshInstance3D] = []
var _box_mesh := BoxMesh.new()


## Simulation (X, Y, Z), Z up -> Godot (X, Z, Y).
static func to_godot(v: Array) -> Vector3:
	return Vector3(v[0], v[2], v[1])


func build_from_json(map: Dictionary) -> void:
	for c in get_children():
		c.queue_free()
	solid_nodes.clear()
	map_name = map.get("name", "")
	_build_environment()
	var interiors: Array[AABB] = []
	for s in map.get("solids", []):
		var a := to_godot(s["min"])
		var b := to_godot(s["max"])
		var box := AABB(a, b - a).abs()
		var mat := TfpsMaterials.material_id(s.get("material", "concrete"))
		var tag: String = s.get("tag", "")
		var mi := MeshInstance3D.new()
		mi.mesh = _box_mesh
		mi.position = box.get_center()
		mi.scale = box.size
		mi.material_override = TfpsMaterials.get_material(mat)
		if mat != TfpsMaterials.GLASS:
			mi.set_instance_shader_parameter("tint", TfpsMaterials.tint_for(mat, tag, box.position))
			mi.set_instance_shader_parameter("seed", float(absi(hash(box.position)) % 997) * 0.37)
		else:
			mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		mi.gi_mode = GeometryInstance3D.GI_MODE_STATIC
		add_child(mi)
		solid_nodes.append(mi)
		# Horizontal slabs high up are ceilings / roofs: light the space under them.
		if box.size.y < 0.6 and box.position.y > 2.4 and box.size.x * box.size.z > 12.0 and tag != "ground":
			interiors.append(box)
	_build_interior_lights(interiors)


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
	env.ssao_enabled = true
	env.ssao_radius = 1.2
	env.ssao_intensity = 2.0
	env.ssil_enabled = true
	env.sdfgi_enabled = true
	env.sdfgi_use_occlusion = true
	env.sdfgi_cascades = 4
	env.sdfgi_min_cell_size = 0.2
	env.glow_enabled = true
	env.glow_intensity = 0.6
	env.glow_bloom = 0.05
	env.volumetric_fog_enabled = true
	env.volumetric_fog_density = 0.012
	env.volumetric_fog_albedo = Color(0.9, 0.88, 0.85)
	env.volumetric_fog_anisotropy = 0.6
	env.volumetric_fog_length = 80.0
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
				l.shadow_enabled = placed < 16
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
