## Architectural and industrial dressing over the gameplay solids, drawn
## only (no collision, never sent to the server): the solids stay the
## authority for movement, shots and destruction. When a solid starts to
## break, the dressing attached to it is hidden.
##
## * Containers: corrugated sheet panels (real geometry) in place of flat
##   boxes, with corner posts, rails, corner castings and door locking bars,
##   in container paint colours.
## * Buildings: plinths, string courses between floors, parapet coping,
##   window sills and lintels, downpipes at the corners, and wall units
##   (air conditioners, electrical boxes, vents, lamps) on outer faces.
class_name TfpsDressing
extends RefCounted

const CONTAINER_PAINTS := [
	Color(0.42, 0.12, 0.08), Color(0.09, 0.2, 0.36), Color(0.12, 0.26, 0.16),
	Color(0.58, 0.32, 0.08), Color(0.48, 0.48, 0.45), Color(0.24, 0.24, 0.26),
	Color(0.55, 0.12, 0.1), Color(0.1, 0.32, 0.42),
]

static var _corrugated := {}   # "LxHxT:axis" -> ArrayMesh
static var _box := BoxMesh.new()
static var _cyl: CylinderMesh


## True if the wall tag is a building's outer wall ("wall_n", "wall_s2", ...).
static func is_outer_wall(tag: String) -> bool:
	if not tag.begins_with("wall_") or tag.ends_with("_lintel") or tag.ends_with("_sill"):
		return false
	var rest := tag.substr(5)
	return rest.length() >= 1 and "nsew".contains(rest[0]) and (rest.length() == 1 or rest.substr(1).is_valid_int())


## Corrugated sheet mesh for a container panel of `size` (Godot axes): ribs
## run vertically on walls, across the short side on roofs.
static func container_panel(size: Vector3) -> Mesh:
	var thin := 0
	for i in 3:
		if size[i] < size[thin]:
			thin = i
	var key := "%.2fx%.2fx%.2f" % [size.x, size.y, size.z]
	if _corrugated.has(key):
		return _corrugated[key]
	# Profile along axis `u` (the long horizontal one for walls; for roofs the
	# long one too, ribs across it), relief along the thin axis.
	var u := 0 if thin != 0 else 2
	if thin == 1:
		u = 0 if size.x >= size.z else 2
	var v := 1 if thin != 1 else (2 if u == 0 else 0)
	var length := size[u]
	var height := size[v]
	var depth := minf(0.036, size[thin] * 0.45)
	var pitch := 0.28
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	# Trapezoid wave: flat 0.07, slope 0.035, flat 0.07, slope 0.035 (x2 = pitch).
	var xs: Array[float] = []
	var zs: Array[float] = []
	var n := int(ceil(length / pitch))
	var start := -length * 0.5
	for k in n:
		var x0 := start + k * pitch
		for seg in [[0.0, 0.0], [0.07, 0.0], [0.105, 1.0], [0.175, 1.0], [0.21, 0.0]]:
			var x := x0 + float(seg[0]) * (pitch / 0.28)
			if x > -start:
				break
			xs.append(x)
			zs.append(float(seg[1]))
	xs.append(-start)
	zs.append(0.0)
	var half := size[thin] * 0.5
	for side in [1.0, -1.0]:
		for k in xs.size() - 1:
			var za: float = (half - zs[k] * depth) * side
			var zb: float = (half - zs[k + 1] * depth) * side
			_quad(st, u, v, thin, xs[k], xs[k + 1], za, zb, -height * 0.5, height * 0.5, side)
	# Edges: top and bottom caps, and the two ends.
	for k in xs.size() - 1:
		for e in [-1.0, 1.0]:
			var y: float = height * 0.5 * e
			var a1 := _v(u, v, thin, xs[k], y, (half - zs[k] * depth))
			var b1 := _v(u, v, thin, xs[k + 1], y, (half - zs[k + 1] * depth))
			var a2 := _v(u, v, thin, xs[k], y, -(half - zs[k] * depth))
			var b2 := _v(u, v, thin, xs[k + 1], y, -(half - zs[k + 1] * depth))
			var nrm := Vector3.ZERO
			nrm[v] = e
			_tri_n(st, [a1, b1, b2], nrm)
			_tri_n(st, [a1, b2, a2], nrm)
	var mesh := st.commit()
	_corrugated[key] = mesh
	return mesh


static func _v(u: int, v: int, w: int, a: float, b: float, c: float) -> Vector3:
	var p := Vector3.ZERO
	p[u] = a
	p[v] = b
	p[w] = c
	return p


static func _quad(st: SurfaceTool, u: int, v: int, w: int, x0: float, x1: float, z0: float, z1: float, y0: float, y1: float, side: float) -> void:
	var a := _v(u, v, w, x0, y0, z0)
	var b := _v(u, v, w, x1, y0, z1)
	var c := _v(u, v, w, x1, y1, z1)
	var d := _v(u, v, w, x0, y1, z0)
	var nrm := (b - a).cross(d - a).normalized()
	if nrm[w] * side < 0.0:
		nrm = -nrm
	_tri_n(st, [a, b, c], nrm)
	_tri_n(st, [a, c, d], nrm)


static func _tri_n(st: SurfaceTool, pts: Array, nrm: Vector3) -> void:
	# Wind triangles so their front faces the normal.
	var a: Vector3 = pts[0]
	var b: Vector3 = pts[1]
	var c: Vector3 = pts[2]
	if (b - a).cross(c - a).dot(nrm) > 0.0:
		var t := b
		b = c
		c = t
	for p in [a, b, c]:
		st.set_normal(nrm)
		st.add_vertex(p)


static func container_tint(box: AABB) -> Color:
	var h := absi(hash(Vector3i((box.get_center() / 3.0).floor()))) % 1000
	var c: Color = CONTAINER_PAINTS[h % CONTAINER_PAINTS.size()]
	return Color(c.r / 0.36, c.g / 0.40, c.b / 0.44)   # relative to the metal base colour


# ---------------------------------------------------------------------------
# Building the dressing
# ---------------------------------------------------------------------------

## Adds the dressing for `solids` under `world`; returns solid index ->
## Array of nodes (hidden when that solid breaks).
static func build(world: Node3D, solids: Array) -> Dictionary:
	var attached := {}
	var root := Node3D.new()
	root.name = "Dressing"
	world.add_child(root)
	var walls: Array = []
	var containers: Array = []
	for s in solids:
		var tag: String = s.get("tag", "")
		if is_outer_wall(tag):
			walls.append(s)
		elif tag == "container":
			containers.append(s)
	_containers(root, containers, attached)
	_buildings(root, solids, walls, attached)
	return attached


static func _aabb(s: Dictionary) -> AABB:
	var a := TfpsNet.vec(s["min"])
	var b := TfpsNet.vec(s["max"])
	return AABB(a, b - a).abs()


static func _add_box(root: Node3D, attached: Dictionary, index: int, size: Vector3, centre: Vector3, mat: int,
		tint := Color(1, 1, 1), shadow := true) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	mi.mesh = _box
	mi.scale = size
	mi.position = centre
	mi.material_override = TfpsMaterials.get_material(mat)
	mi.set_instance_shader_parameter("tint", tint)
	mi.set_instance_shader_parameter("seed", float(absi(hash(Vector3i(centre.round()))) % 997) * 0.37)
	mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON if shadow else GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	root.add_child(mi)
	if index >= 0:
		if not attached.has(index):
			attached[index] = []
		attached[index].append(mi)
	return mi


static func _add_cyl(root: Node3D, attached: Dictionary, index: int, radius: float, a: Vector3, b: Vector3, mat: int,
		tint := Color(1, 1, 1)) -> MeshInstance3D:
	if _cyl == null:
		_cyl = CylinderMesh.new()
		_cyl.top_radius = 0.5
		_cyl.bottom_radius = 0.5
		_cyl.height = 1.0
		_cyl.radial_segments = 12
		_cyl.rings = 1
	var mi := MeshInstance3D.new()
	mi.mesh = _cyl
	var dir := b - a
	var y := dir.normalized()
	var x := y.cross(Vector3.FORWARD if absf(y.z) < 0.9 else Vector3.RIGHT).normalized()
	var z := x.cross(y)
	mi.transform = Transform3D(Basis(x * radius * 2.0, y * dir.length(), z * radius * 2.0), (a + b) * 0.5)
	mi.material_override = TfpsMaterials.get_material(mat)
	mi.set_instance_shader_parameter("tint", tint)
	root.add_child(mi)
	if index >= 0:
		if not attached.has(index):
			attached[index] = []
		attached[index].append(mi)
	return mi


## Paint colour per container panel (solid index -> tint): the panels of
## one container share it.
static func container_tints(solids: Array) -> Dictionary:
	var out := {}
	var panels: Array = []
	for s in solids:
		if s.get("tag", "") == "container":
			panels.append(s)
	for g in _group(panels):
		var tint := container_tint((g[0] as AABB).grow(-0.06))
		for i in g[1]:
			out[i] = tint
	return out


static func _group(panels: Array) -> Array:
	var groups: Array = []   # [AABB, [indices]]
	for s in panels:
		var box := _aabb(s).grow(0.06)
		var merged := false
		for g in groups:
			if (g[0] as AABB).intersects(box):
				g[0] = (g[0] as AABB).merge(box)
				g[1].append(int(s["index"]))
				merged = true
				break
		if not merged:
			groups.append([box, [int(s["index"])]])
	return groups


# Containers: frame each one (posts, rails, castings, door bars).
static func _containers(root: Node3D, panels: Array, attached: Dictionary) -> void:
	for g in _group(panels):
		var b: AABB = (g[0] as AABB).grow(-0.06)
		if b.size.y < 2.0 or maxf(b.size.x, b.size.z) < 2.0:
			continue
		var tint := container_tint(b)
		var frame := Color(tint.r * 0.8, tint.g * 0.8, tint.b * 0.8)
		var idx: int = g[1][0]
		var lo := b.position
		var hi := b.end
		var post := 0.16
		for cx in [lo.x + post * 0.5, hi.x - post * 0.5]:
			for cz in [lo.z + post * 0.5, hi.z - post * 0.5]:
				_add_box(root, attached, idx, Vector3(post, b.size.y, post), Vector3(cx, b.get_center().y, cz), TfpsMaterials.METAL, frame)
				for cy in [lo.y + 0.06, hi.y - 0.06]:
					_add_box(root, attached, idx, Vector3(0.19, 0.13, 0.19), Vector3(cx, cy, cz), TfpsMaterials.METAL, Color(0.5, 0.5, 0.5))
		var along_x := b.size.x >= b.size.z
		var length := b.size.x if along_x else b.size.z
		for cy in [lo.y + 0.08, hi.y - 0.07]:
			for side in [-1.0, 1.0]:
				var c := b.get_center()
				c.y = cy
				var size: Vector3
				if along_x:
					c.z += side * (b.size.z * 0.5 - 0.06)
					size = Vector3(length, 0.15, 0.13)
				else:
					c.x += side * (b.size.x * 0.5 - 0.06)
					size = Vector3(0.13, 0.15, length)
				_add_box(root, attached, idx, size, c, TfpsMaterials.METAL, frame)
		# Door end: four locking bars with handles.
		var width := b.size.z if along_x else b.size.x
		for k in 4:
			var t := -0.36 + k * 0.24
			var p := b.get_center()
			if along_x:
				p.x = hi.x + 0.03
				p.z += t * width
			else:
				p.z = hi.z + 0.03
				p.x += t * width
			_add_cyl(root, attached, idx, 0.018, Vector3(p.x, lo.y + 0.12, p.z), Vector3(p.x, hi.y - 0.12, p.z), TfpsMaterials.METAL, Color(0.6, 0.6, 0.6))
			var h := p + (Vector3(0.05, 0, 0) if along_x else Vector3(0, 0, 0.05))
			h.y = lo.y + 1.1
			_add_box(root, attached, idx, Vector3(0.04, 0.05, 0.22) if along_x else Vector3(0.22, 0.05, 0.04), h, TfpsMaterials.METAL, Color(0.55, 0.55, 0.55))


# Buildings.
## Outward direction of an outer wall from its compass letter (sim north
## +Y is Godot +Z, east +X); centroid of nearby walls otherwise.
static func _outward_of(tag: String, box: AABB, walls: Array) -> Vector3:
	var base := tag.trim_suffix("_lintel").trim_suffix("_sill")
	if base.begins_with("wall_") and base.length() > 5:
		match base[5]:
			"n": return Vector3(0, 0, 1)
			"s": return Vector3(0, 0, -1)
			"e": return Vector3(1, 0, 0)
			"w": return Vector3(-1, 0, 0)
	return _outward(box, walls)


static func _outward(box: AABB, walls: Array) -> Vector3:
	var centre := Vector3.ZERO
	var n := 0
	for w in walls:
		var c := _aabb(w).get_center()
		if c.distance_to(box.get_center()) < 28.0:
			centre += c
			n += 1
	centre /= maxf(1, n)
	var d := box.get_center() - centre
	if box.size.x < box.size.z:
		return Vector3(signf(d.x) if d.x != 0.0 else 1.0, 0, 0)
	return Vector3(0, 0, signf(d.z) if d.z != 0.0 else 1.0)


const ROOM_PAINTS := [Color(1.0, 1.0, 1.0), Color(0.96, 0.95, 0.9), Color(0.9, 0.93, 0.92),
	Color(0.97, 0.92, 0.85), Color(0.88, 0.9, 0.95), Color(0.92, 0.95, 0.88)]


static func room_paint(p: Vector3) -> Color:
	var h := absi(hash(Vector3i((p / Vector3(24.0, 3.0, 24.0)).floor())))
	return ROOM_PAINTS[h % ROOM_PAINTS.size()]


## Plaster lining on one face (direction `inward`) of a wall, with a skirting board.
static func _lining(root: Node3D, attached: Dictionary, idx: int, box: AABB, inward: Vector3) -> void:
	var along_x := box.size.x >= box.size.z
	var length := box.size.x if along_x else box.size.z
	var thick := box.size.z if along_x else box.size.x
	var c := box.get_center() + inward * (thick * 0.5 + 0.006)
	var size := Vector3(length, box.size.y - 0.01, 0.012) if along_x else Vector3(0.012, box.size.y - 0.01, length)
	_add_box(root, attached, idx, size, c, TfpsMaterials.DRYWALL, room_paint(c), false)
	_skirting(root, attached, idx, box, inward, 0.012)


static func _skirting(root: Node3D, attached: Dictionary, idx: int, box: AABB, normal: Vector3, offset := 0.0) -> void:
	var along_x := box.size.x >= box.size.z
	var length := box.size.x if along_x else box.size.z
	var thick := box.size.z if along_x else box.size.x
	var c := box.get_center() + normal * (thick * 0.5 + offset + 0.008)
	c.y = box.position.y + 0.06
	var size := Vector3(length, 0.1, 0.016) if along_x else Vector3(0.016, 0.1, length)
	_add_box(root, attached, idx, size, c, TfpsMaterials.WOOD, Color(0.55, 0.5, 0.48), false)


static func _buildings(root: Node3D, solids: Array, walls: Array, attached: Dictionary) -> void:
	var ground_y := 0.0
	var tops := {}   # wall index -> true when no outer wall sits on it
	for w in walls:
		var box := _aabb(w)
		var top := true
		for o in walls:
			var ob := _aabb(o)
			if absf(ob.position.y - box.end.y) < 0.15 and ob.intersects(box.grow(0.2)) and o != w:
				top = false
				break
		tops[int(w["index"])] = top
	var concrete_dark := Color(0.72, 0.71, 0.68)
	var concrete_light := Color(1.18, 1.16, 1.1)
	var rng := RandomNumberGenerator.new()
	for w in walls:
		var box := _aabb(w)
		var idx := int(w["index"])
		var out := _outward_of(w.get("tag", ""), box, walls)
		var along_x := box.size.x >= box.size.z
		var length := box.size.x if along_x else box.size.z
		var thick := box.size.z if along_x else box.size.x
		var c := box.get_center()
		var size_along := func(l: float, h: float, t: float) -> Vector3:
			return Vector3(l, h, t) if along_x else Vector3(t, h, l)
		# Plinth on ground-floor walls.
		if box.position.y < ground_y + 0.4:
			_add_box(root, attached, idx, size_along.call(length + 0.02, 0.38, thick + 0.06),
				Vector3(c.x, box.position.y + 0.19, c.z), TfpsMaterials.CONCRETE, concrete_dark)
		if tops[idx]:
			# Parapet coping with a slight overhang.
			_add_box(root, attached, idx, size_along.call(length + 0.06, 0.07, thick + 0.1),
				Vector3(c.x, box.end.y + 0.035, c.z), TfpsMaterials.CONCRETE, concrete_light)
		else:
			# String course between floors.
			_add_box(root, attached, idx, size_along.call(length + 0.02, 0.16, thick + 0.06),
				Vector3(c.x, box.end.y - 0.08, c.z), TfpsMaterials.CONCRETE, concrete_light)
		var face := c + out * (thick * 0.5)
		# Downpipe at the wall's end (corners) on top-floor walls, ground to top.
		if tops[idx] and length > 3.0:
			var end_pos := face + out * 0.07
			if along_x:
				end_pos.x = box.end.x - 0.12
			else:
				end_pos.z = box.end.z - 0.12
			_add_cyl(root, attached, idx, 0.045, Vector3(end_pos.x, ground_y + 0.05, end_pos.z),
				Vector3(end_pos.x, box.end.y - 0.05, end_pos.z), TfpsMaterials.METAL, Color(0.75, 0.75, 0.74))
			var gutter := Vector3(c.x, box.end.y - 0.04, c.z) + out * (thick * 0.5 + 0.08)
			_add_box(root, attached, idx, size_along.call(length, 0.1, 0.12), gutter, TfpsMaterials.METAL, Color(0.75, 0.75, 0.74))
		# Wall units on long outer faces: air conditioner, electrical box, lamp.
		rng.seed = idx * 7919 + 13
		if length > 4.0:
			var units := rng.randi_range(0, 2)
			for k in units:
				var t := rng.randf_range(-0.38, 0.38) * length
				var p := face
				if along_x:
					p.x += t
				else:
					p.z += t
				var kind := rng.randi_range(0, 2)
				if kind == 0:   # air conditioner on a bracket
					var unit: Vector3 = size_along.call(0.85, 0.6, 0.32)
					_add_box(root, attached, idx, unit, Vector3(p.x, box.position.y + 2.35, p.z) + out * 0.17, TfpsMaterials.METAL, Color(2.1, 2.1, 2.05))
					_add_box(root, attached, idx, size_along.call(0.5, 0.42, 0.01), Vector3(p.x, box.position.y + 2.35, p.z) + out * 0.335, TfpsMaterials.METAL, Color(0.35, 0.35, 0.35))
					_add_cyl(root, attached, idx, 0.012, Vector3(p.x, box.position.y + 2.05, p.z) + out * 0.03, Vector3(p.x, box.position.y + 0.4, p.z) + out * 0.03, TfpsMaterials.METAL, Color(1.6, 1.6, 1.6))
				elif kind == 1:   # electrical box with a conduit
					_add_box(root, attached, idx, size_along.call(0.45, 0.6, 0.18), Vector3(p.x, box.position.y + 1.4, p.z) + out * 0.09, TfpsMaterials.METAL, Color(1.3, 1.3, 1.25))
					_add_cyl(root, attached, idx, 0.02, Vector3(p.x, box.position.y + 1.7, p.z) + out * 0.04, Vector3(p.x, box.position.y + 2.9, p.z) + out * 0.04, TfpsMaterials.METAL, Color(1.1, 1.1, 1.1))
				else:   # wall lamp
					var lamp := _add_box(root, attached, idx, size_along.call(0.22, 0.14, 0.16), Vector3(p.x, box.position.y + 2.7, p.z) + out * 0.08, TfpsMaterials.METAL, Color(0.5, 0.5, 0.5))
					var glow := MeshInstance3D.new()
					var gm := BoxMesh.new()
					gm.size = size_along.call(0.18, 0.02, 0.12)
					glow.mesh = gm
					var em := StandardMaterial3D.new()
					em.albedo_color = Color(1.0, 0.9, 0.7)
					em.emission_enabled = true
					em.emission = Color(1.0, 0.85, 0.6)
					em.emission_energy_multiplier = 2.0
					glow.material_override = em
					glow.position = Vector3(0, -0.08, 0) / lamp.scale
					glow.scale = Vector3.ONE / lamp.scale
					lamp.add_child(glow)
	# Interiors of houses (outer walls with a floor number): plaster on the
	# inner face with a skirting board; room paint varies.
	for w in walls:
		var tag: String = w.get("tag", "")
		if not tag.substr(tag.length() - 1).is_valid_int():
			continue
		var box := _aabb(w)
		var idx := int(w["index"])
		_lining(root, attached, idx, box, -_outward_of(tag, box, walls))
	for s in solids:
		if int(s.get("material", -1)) != TfpsMaterials.DRYWALL:
			continue
		var box := _aabb(s)
		if box.size.y < 1.5 or minf(box.size.x, box.size.z) > 0.4:
			continue
		var idx := int(s["index"])
		var along_x := box.size.x >= box.size.z
		for side in [-1.0, 1.0]:
			var n := Vector3(0, 0, side) if along_x else Vector3(side, 0, 0)
			_skirting(root, attached, idx, box, n)
	# Window sills and lintels of outer walls.
	for s in solids:
		var tag: String = s.get("tag", "")
		var is_sill := tag.ends_with("_sill")
		var is_lintel := tag.ends_with("_lintel")
		if not (is_sill or is_lintel):
			continue
		var base := tag.trim_suffix("_sill").trim_suffix("_lintel")
		if not is_outer_wall(base):
			continue
		var box := _aabb(s)
		var idx := int(s["index"])
		var along_x := box.size.x >= box.size.z
		var length := box.size.x if along_x else box.size.z
		var thick := box.size.z if along_x else box.size.x
		var c := box.get_center()
		var size := (Vector3(length + 0.12, 0.06, thick + 0.1) if along_x else Vector3(thick + 0.1, 0.06, length + 0.12))
		if base.substr(base.length() - 1).is_valid_int():
			_lining(root, attached, idx, box, -_outward_of(tag, box, walls))
		if is_sill:
			_add_box(root, attached, idx, size, Vector3(c.x, box.end.y + 0.02, c.z), TfpsMaterials.CONCRETE, concrete_light)
		else:
			size.y = 0.14
			_add_box(root, attached, idx, size, Vector3(c.x, box.position.y + 0.07, c.z), TfpsMaterials.CONCRETE, concrete_light)
