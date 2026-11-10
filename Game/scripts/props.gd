## Dresses map solids with the Blender assets (Assets/Exports, imported from
## FBX): a solid whose tag and size match an asset is drawn as that asset,
## turned to the solid's long axis and scaled to fit it exactly. The solid
## stays the gameplay collision; when one of its cells breaks the prop is
## replaced by the cells.
class_name TfpsProps
extends RefCounted

# tag -> [asset, nominal size of the solid (Godot x, y, z), optional vertical
# offset of the model's origin below the solid's bottom (metres, at nominal size)]
const DRESSING := {
	"crate": ["WoodCrate", Vector3(1.248, 1.202, 1.248)],
	"barrier": ["JerseyBarrier", Vector3(0.6, 0.93, 3.0)],
	"container": ["ShippingContainer", Vector3(6.061, 2.6, 2.532)],
	"door": ["WoodDoor", Vector3(1.0, 2.15, 0.116)],
	# The car solid is the body box 0.25-1.0 m up; the model has its wheels
	# below it and the cabin (a solid of its own) above.
	"car": ["Sedan", Vector3(1.8, 0.75, 4.5), -0.25],
}

static var _scenes := {}
static var _materials := {}


static func available(asset: String) -> bool:
	return ResourceLoader.exists("res://assets/%s/SM_%s.fbx" % [asset, asset])


## A ready-to-place instance of an asset with its PBR material (null if the
## assets were not synced into Game/assets).
static func instance(asset: String) -> Node3D:
	if not available(asset):
		return null
	if not _scenes.has(asset):
		_scenes[asset] = load("res://assets/%s/SM_%s.fbx" % [asset, asset])
	var root: Node3D = (_scenes[asset] as PackedScene).instantiate()
	var mat := material(asset)
	for n in root.find_children("*", "MeshInstance3D", true, false):
		var mi := n as MeshInstance3D
		if mi.name.begins_with("UCX_") or String(mi.get_parent().name).begins_with("UCX_"):
			mi.visible = false  # collision primitive
			continue
		mi.material_override = mat
		mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON
	return root


static func material(asset: String) -> Material:
	if _materials.has(asset):
		return _materials[asset]
	var m := ORMMaterial3D.new()
	var base := "res://assets/%s/T_%s_" % [asset, asset]
	m.albedo_texture = load(base + "BC.png")
	if ResourceLoader.exists(base + "N.png"):
		m.normal_enabled = true
		m.normal_texture = load(base + "N.png")
	if ResourceLoader.exists(base + "ORM.png"):
		m.orm_texture = load(base + "ORM.png")
	m.texture_filter = BaseMaterial3D.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC
	_materials[asset] = m
	return m


## Returns the prop to draw for a solid, or null to draw the box.
static func for_solid(tag: String, box: AABB) -> Node3D:
	if not DRESSING.has(tag):
		return null
	var entry: Array = DRESSING[tag]
	var asset: String = entry[0]
	var nominal: Vector3 = entry[1]
	# Does the solid have the asset's proportions, in either orientation?
	var turned := false
	var size := box.size
	var fits := _close(size, nominal)
	if not fits and _close(Vector3(size.z, size.y, size.x), nominal):
		fits = true
		turned = true
	if not fits:
		return null
	var p := instance(asset)
	if p == null:
		return null
	var drop: float = entry[2] if entry.size() > 2 else 0.0
	p.position = Vector3(box.get_center().x, box.position.y + drop * size.y / nominal.y, box.get_center().z)
	if turned:
		p.rotation.y = PI * 0.5
		p.scale = Vector3(size.z / nominal.x, size.y / nominal.y, size.x / nominal.z)
	else:
		p.scale = Vector3(size.x / nominal.x, size.y / nominal.y, size.z / nominal.z)
	return p


## Window frame around a glass pane (the glass stays visible inside it).
static func window_frame(box: AABB) -> Node3D:
	var p := instance("WindowFrame")
	if p == null:
		return null
	var nominal := Vector3(1.5, 1.15, 0.35)
	var along_x := box.size.x >= box.size.z
	var width := box.size.x if along_x else box.size.z
	p.position = Vector3(box.get_center().x, box.position.y, box.get_center().z)
	if not along_x:
		p.rotation.y = PI * 0.5
	p.scale = Vector3(width / nominal.x, box.size.y / nominal.y, 1.0)
	return p


static func _close(a: Vector3, b: Vector3) -> bool:
	for i in 3:
		if absf(a[i] - b[i]) > b[i] * 0.25 + 0.05:
			return false
	return true
