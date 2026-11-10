## Materials for map geometry, one per map material, built on the procedural
## world surface shader (no texture files).
class_name TfpsMaterials
extends RefCounted

const CONCRETE := 0
const BRICK := 1
const WOOD := 2
const DRYWALL := 3
const METAL := 4
const GLASS := 5
const REINFORCED := 6
const DIRT := 7

const NAMES := {
	"concrete": CONCRETE, "brick": BRICK, "wood": WOOD, "drywall": DRYWALL,
	"metal": METAL, "glass": GLASS, "reinforced": REINFORCED, "dirt": DIRT,
}

static var _cache := {}
static var _shader: Shader


static func material_id(name: String) -> int:
	return NAMES.get(name, CONCRETE)


static func get_material(id: int) -> Material:
	if _cache.has(id):
		return _cache[id]
	var m: Material
	if id == GLASS:
		var g := StandardMaterial3D.new()
		g.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		g.albedo_color = Color(0.62, 0.74, 0.78, 0.22)
		g.roughness = 0.04
		g.metallic_specular = 0.9
		g.cull_mode = BaseMaterial3D.CULL_DISABLED
		m = g
	else:
		if _shader == null:
			_shader = load("res://shaders/world_surface.gdshader")
		var s := ShaderMaterial.new()
		s.shader = _shader
		var p := _params(id)
		for k in p:
			s.set_shader_parameter(k, p[k])
		m = s
	_cache[id] = m
	return m


static func _params(id: int) -> Dictionary:
	match id:
		BRICK:
			return {"pattern": 1, "color_a": Color(0.36, 0.14, 0.09), "color_b": Color(0.58, 0.56, 0.52),
				"roughness_base": 0.9, "bump_strength": 1.6}
		WOOD:
			return {"pattern": 2, "color_a": Color(0.47, 0.32, 0.19), "color_b": Color(0.20, 0.13, 0.08),
				"roughness_base": 0.7, "bump_strength": 1.0}
		DRYWALL:
			return {"pattern": 3, "color_a": Color(0.80, 0.78, 0.74), "color_b": Color(0.70, 0.68, 0.64),
				"roughness_base": 0.9, "bump_strength": 0.4}
		METAL:
			return {"pattern": 4, "color_a": Color(0.36, 0.40, 0.44), "color_b": Color(0.33, 0.37, 0.41),
				"roughness_base": 0.6, "metallic_base": 0.05, "bump_strength": 1.2}
		REINFORCED:
			return {"pattern": 6, "color_a": Color(0.30, 0.31, 0.32), "color_b": Color(0.42, 0.43, 0.44),
				"roughness_base": 0.45, "metallic_base": 0.9, "bump_strength": 1.0}
		DIRT:
			return {"pattern": 7, "color_a": Color(0.40, 0.39, 0.37), "color_b": Color(0.11, 0.11, 0.11),
				"roughness_base": 0.93, "bump_strength": 1.4}
		_:
			return {"pattern": 0, "color_a": Color(0.60, 0.59, 0.56), "color_b": Color(0.42, 0.41, 0.39),
				"roughness_base": 0.85, "bump_strength": 0.8}


## Per-object paint (instance tint) so repeated props are not identical.
static func tint_for(material: int, tag: String, key: Vector3) -> Color:
	var h := absi(hash(Vector3i(key.round()))) % 1000 / 1000.0
	if tag == "container":
		var paints := [Color(0.62, 0.16, 0.12), Color(0.12, 0.28, 0.50), Color(0.16, 0.38, 0.22),
			Color(0.75, 0.42, 0.10), Color(0.55, 0.55, 0.52), Color(0.30, 0.18, 0.40)]
		var c: Color = paints[int(h * paints.size()) % paints.size()]
		return Color(c.r / 0.36, c.g / 0.40, c.b / 0.44)  # relative to the metal base colour
	if material == METAL and (tag == "bollard" or tag == "barrier"):
		return Color(2.4, 1.8, 0.3)  # safety yellow
	if material == WOOD and tag != "door":
		return Color(1.0, 1.0, 1.0).lerp(Color(0.85, 0.82, 0.8), h)
	return Color(1, 1, 1)
