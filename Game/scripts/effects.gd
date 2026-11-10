## Visual effects: tracers, muzzle flashes, impact particles and bullet
## holes per surface, explosions, breaking cells, flashbangs and volumetric
## smoke clouds. All textures are generated here (no files).
class_name TfpsEffects
extends Node3D

const MAX_DECALS := 160

var _soft_dot: Texture2D
var _hole_tex: Texture2D
var _decals: Array[Decal] = []
var _clouds := {}   # gadget id -> FogVolume
var shake := 0.0    # camera shake amount, read by the player camera
var _shells: Array = []   # [MeshInstance3D, velocity, spin axis, age]
var _shell_mesh: CylinderMesh
const MAX_SHELLS := 24


func _ready() -> void:
	_soft_dot = _radial_texture(64, Color(1, 1, 1, 1), Color(1, 1, 1, 0))
	_hole_tex = _bullet_hole_texture()


static func _radial_texture(size: int, inner: Color, outer: Color) -> GradientTexture2D:
	var g := Gradient.new()
	g.set_color(0, inner)
	g.set_color(1, outer)
	var t := GradientTexture2D.new()
	t.gradient = g
	t.fill = GradientTexture2D.FILL_RADIAL
	t.fill_from = Vector2(0.5, 0.5)
	t.fill_to = Vector2(0.5, 0.0)
	t.width = size
	t.height = size
	return t


static func _bullet_hole_texture() -> ImageTexture:
	var n := 64
	var img := Image.create(n, n, false, Image.FORMAT_RGBA8)
	var rng := RandomNumberGenerator.new()
	rng.seed = 7
	for y in n:
		for x in n:
			var d := Vector2(x - n * 0.5, y - n * 0.5).length() / (n * 0.5)
			var jag := 0.08 * sin(atan2(y - n * 0.5, x - n * 0.5) * 7.0)
			var a := 0.0
			var c := 0.0
			if d < 0.22 + jag:
				a = 1.0; c = 0.02           # hole
			elif d < 0.55 + jag * 1.5:
				a = 0.85 * (1.0 - (d - 0.22) / 0.35); c = 0.12 + rng.randf() * 0.05  # scorched rim
			elif d < 1.0:
				a = 0.25 * (1.0 - d) * rng.randf(); c = 0.3      # dust ring
			img.set_pixel(x, y, Color(c, c * 0.95, c * 0.9, clampf(a, 0.0, 1.0)))
	return ImageTexture.create_from_image(img)


func _particles(amount: int, lifetime: float, at: Vector3, dir: Vector3, color: Color, emissive: float,
		speed: Vector2, spread: float, size: Vector2, gravity: float, drag: float, explosive := 1.0) -> GPUParticles3D:
	var p := GPUParticles3D.new()
	p.amount = amount
	p.lifetime = lifetime
	p.one_shot = true
	p.explosiveness = explosive
	p.local_coords = false
	var pm := ParticleProcessMaterial.new()
	pm.direction = dir.normalized() if dir.length() > 0.01 else Vector3.UP
	pm.spread = spread
	pm.initial_velocity_min = speed.x
	pm.initial_velocity_max = speed.y
	pm.gravity = Vector3(0, -9.8 * gravity, 0)
	pm.damping_min = drag
	pm.damping_max = drag * 1.3
	pm.scale_min = size.x
	pm.scale_max = size.y
	var curve := Curve.new()
	curve.add_point(Vector2(0, 0.6))
	curve.add_point(Vector2(0.3, 1.0))
	curve.add_point(Vector2(1, 1.6))
	var ct := CurveTexture.new()
	ct.curve = curve
	pm.scale_curve = ct
	var fade := Gradient.new()
	fade.set_color(0, Color(1, 1, 1, 1))
	fade.set_color(1, Color(1, 1, 1, 0))
	var ft := GradientTexture1D.new()
	ft.gradient = fade
	pm.color_ramp = ft
	pm.collision_mode = ParticleProcessMaterial.COLLISION_RIGID
	pm.collision_bounce = 0.3
	pm.collision_friction = 0.6
	p.process_material = pm
	var quad := QuadMesh.new()
	quad.size = Vector2(1, 1)
	var m := StandardMaterial3D.new()
	m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	m.billboard_mode = BaseMaterial3D.BILLBOARD_PARTICLES
	m.vertex_color_use_as_albedo = true
	m.albedo_color = color
	m.albedo_texture = _soft_dot
	m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED if emissive > 0.0 else BaseMaterial3D.SHADING_MODE_PER_PIXEL
	if emissive > 0.0:
		m.emission_enabled = true
		m.emission = color
		m.emission_energy_multiplier = emissive
		m.blend_mode = BaseMaterial3D.BLEND_MODE_ADD
	m.proximity_fade_enabled = true
	m.proximity_fade_distance = 0.3
	quad.material = m
	p.draw_pass_1 = quad
	p.position = at
	p.emitting = true
	add_child(p)
	get_tree().create_timer(lifetime + 0.5).timeout.connect(p.queue_free)
	return p


func _flash_light(at: Vector3, color: Color, energy: float, radius: float, life: float) -> void:
	var l := OmniLight3D.new()
	l.position = at
	l.light_color = color
	l.light_energy = energy
	l.omni_range = radius
	l.shadow_enabled = false
	add_child(l)
	var tw := create_tween()
	tw.tween_property(l, "light_energy", 0.0, life)
	tw.tween_callback(l.queue_free)


func tracer(from: Vector3, to: Vector3) -> void:
	var len := from.distance_to(to)
	if len < 1.0:
		return
	var mi := MeshInstance3D.new()
	var cm := CylinderMesh.new()
	cm.top_radius = 0.006
	cm.bottom_radius = 0.006
	cm.height = minf(len, 6.0)
	cm.radial_segments = 4
	mi.mesh = cm
	var m := StandardMaterial3D.new()
	m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	m.albedo_color = Color(1.0, 0.85, 0.55)
	m.emission_enabled = true
	m.emission = Color(1.0, 0.75, 0.4)
	m.emission_energy_multiplier = 6.0
	mi.material_override = m
	mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(mi)
	var dir := (to - from).normalized()
	mi.look_at_from_position(from, to, Vector3.UP if absf(dir.y) < 0.99 else Vector3.RIGHT)
	mi.rotate_object_local(Vector3.RIGHT, PI * 0.5)
	# Streak travels along the shot at ~600 m/s.
	var t := len / 600.0
	var tw := create_tween()
	tw.tween_property(mi, "position", to - dir * cm.height * 0.5, maxf(t, 0.02)).from(from + dir * cm.height * 0.5)
	tw.tween_callback(mi.queue_free)


## Muzzle flash and smoke. `local`: our own weapon, a few centimetres from the
## camera, so the flash is smaller and the smoke thin enough to see through.
func muzzle(at: Vector3, dir: Vector3, local := false) -> void:
	_flash_light(at, Color(1.0, 0.7, 0.35), 5.0, 6.0, 0.06)
	if local:
		_particles(5, 0.05, at, dir, Color(1.0, 0.65, 0.25), 5.0, Vector2(2, 5), 10.0, Vector2(0.03, 0.06), 0.0, 4.0)
		_particles(2, 0.6, at + dir.normalized() * 0.15, dir, Color(0.6, 0.6, 0.6, 0.1), 0.0, Vector2(0.8, 1.6), 15.0, Vector2(0.04, 0.08), -0.05, 3.0)
		return
	_particles(6, 0.06, at, dir, Color(1.0, 0.65, 0.25), 6.0, Vector2(2, 6), 12.0, Vector2(0.08, 0.16), 0.0, 4.0)
	_particles(4, 0.8, at, dir, Color(0.6, 0.6, 0.6, 0.35), 0.0, Vector2(0.5, 1.5), 20.0, Vector2(0.1, 0.2), -0.05, 3.0)


## A spent casing thrown from the ejection port (local weapon only): brass for
## cartridges, red for shotgun hulls. Falls with gravity and spins; it is
## cosmetic and fades after a short time.
func shell(at: Vector3, velocity: Vector3, hull := false) -> void:
	if _shell_mesh == null:
		_shell_mesh = CylinderMesh.new()
		_shell_mesh.top_radius = 0.0045
		_shell_mesh.bottom_radius = 0.0048
		_shell_mesh.height = 0.022
		_shell_mesh.radial_segments = 8
		_shell_mesh.rings = 1
	var mi := MeshInstance3D.new()
	mi.mesh = _shell_mesh
	if hull:
		mi.scale = Vector3(2.3, 2.6, 2.3)
		mi.material_override = TfpsModels.mat("shell_hull", Color(0.55, 0.08, 0.06), 0.5, 0.0)
	else:
		mi.material_override = TfpsModels.mat("shell_brass", Color(0.78, 0.6, 0.3), 0.3, 1.0)
	mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	mi.position = at
	mi.rotation = Vector3(randf() * TAU, randf() * TAU, 0)
	add_child(mi)
	var axis := Vector3(randf() - 0.5, randf() - 0.5, randf() - 0.5).normalized()
	_shells.append([mi, velocity, axis, 0.0])
	if _shells.size() > MAX_SHELLS:
		(_shells.pop_front()[0] as Node).queue_free()


func _update_shells(delta: float) -> void:
	for i in range(_shells.size() - 1, -1, -1):
		var sh: Array = _shells[i]
		var mi: MeshInstance3D = sh[0]
		sh[3] += delta
		if sh[3] > 0.9:
			mi.queue_free()
			_shells.remove_at(i)
			continue
		var v: Vector3 = sh[1]
		v.y -= 9.8 * delta
		sh[1] = v
		mi.position += v * delta
		mi.rotate(sh[2], 25.0 * delta)


## Bullet impact; surface: map material id or 254 (player).
func impact(at: Vector3, normal: Vector3, surface: int) -> void:
	var n := normal.normalized()
	match surface:
		254:
			_particles(14, 0.4, at, n, Color(0.4, 0.02, 0.02, 0.9), 0.0, Vector2(1, 3), 35.0, Vector2(0.03, 0.06), 0.6, 2.0)
			return
		4, 6:  # metal: sparks
			_particles(18, 0.35, at, n, Color(1.0, 0.7, 0.3), 8.0, Vector2(4, 10), 50.0, Vector2(0.01, 0.02), 1.0, 0.5)
			_flash_light(at + n * 0.05, Color(1.0, 0.7, 0.4), 1.5, 2.0, 0.08)
		5:  # glass
			_particles(16, 1.2, at, n, Color(0.75, 0.85, 0.9, 0.8), 0.0, Vector2(1.5, 4), 60.0, Vector2(0.015, 0.03), 1.0, 0.3)
		2:  # wood
			_particles(10, 1.0, at, n, Color(0.45, 0.3, 0.16), 0.0, Vector2(2, 5), 40.0, Vector2(0.02, 0.04), 1.0, 0.3)
			_particles(6, 0.9, at, n, Color(0.55, 0.45, 0.35, 0.5), 0.0, Vector2(0.5, 1.5), 30.0, Vector2(0.06, 0.12), 0.05, 3.0)
		_:  # concrete, brick, drywall, dirt
			var dust := Color(0.62, 0.58, 0.52, 0.55) if surface != 1 else Color(0.62, 0.38, 0.3, 0.55)
			_particles(12, 1.2, at, n, dust, 0.0, Vector2(1, 4), 30.0, Vector2(0.08, 0.16), 0.08, 3.0)
			_particles(8, 1.0, at, n, dust.darkened(0.3), 0.0, Vector2(2, 5), 45.0, Vector2(0.015, 0.03), 1.0, 0.3)
	_bullet_hole(at, n)


func _bullet_hole(at: Vector3, n: Vector3) -> void:
	var d := Decal.new()
	d.texture_albedo = _hole_tex
	d.size = Vector3(0.09, 0.08, 0.09)
	d.cull_mask = 1
	d.upper_fade = 0.0
	d.lower_fade = 0.0
	add_child(d)
	# Decals project along their local -Y: point it into the surface.
	var up := n
	var side := up.cross(Vector3.UP if absf(up.y) < 0.9 else Vector3.RIGHT).normalized()
	d.global_transform = Transform3D(Basis(side, up, side.cross(up)).rotated(up, randf() * TAU), at)
	_decals.append(d)
	if _decals.size() > MAX_DECALS:
		_decals.pop_front().queue_free()


func explosion(at: Vector3, radius: float) -> void:
	var r := maxf(radius, 1.0)
	_flash_light(at + Vector3(0, 0.5, 0), Color(1.0, 0.6, 0.3), 16.0, r * 4.0, 0.5)
	_particles(30, 0.5, at, Vector3.UP, Color(1.0, 0.55, 0.2), 5.0, Vector2(r * 1.5, r * 3.0), 90.0, Vector2(r * 0.25, r * 0.45), -0.2, 4.0)
	_particles(36, 4.0, at, Vector3.UP, Color(0.25, 0.24, 0.23, 0.75), 0.0, Vector2(r * 0.5, r * 1.6), 80.0, Vector2(r * 0.35, r * 0.7), -0.1, 2.0)
	_particles(40, 3.0, at, Vector3.UP, Color(0.2, 0.19, 0.18), 0.0, Vector2(4, 12), 70.0, Vector2(0.04, 0.08), 1.0, 0.2)
	_particles(24, 0.7, at, Vector3.UP, Color(1.0, 0.8, 0.4), 8.0, Vector2(6, 14), 90.0, Vector2(0.015, 0.03), 1.0, 0.4)
	shake = maxf(shake, 1.0)


func flash(at: Vector3) -> void:
	_flash_light(at, Color(1.0, 0.98, 0.95), 60.0, 25.0, 0.35)
	_particles(12, 2.5, at, Vector3.UP, Color(0.75, 0.75, 0.75, 0.6), 0.0, Vector2(0.5, 2.0), 90.0, Vector2(0.3, 0.6), -0.1, 2.0)


## A destructible cell broke: chunks of its material and dust.
func breakage(center: Vector3, size: Vector3, material: int) -> void:
	var colors := {0: Color(0.5, 0.49, 0.46), 1: Color(0.5, 0.25, 0.18), 2: Color(0.45, 0.3, 0.16), 3: Color(0.82, 0.8, 0.76),
		4: Color(0.4, 0.42, 0.45), 5: Color(0.75, 0.85, 0.9, 0.8), 6: Color(0.35, 0.36, 0.37), 7: Color(0.4, 0.36, 0.3)}
	var c: Color = colors.get(material, Color(0.5, 0.5, 0.5))
	var vol := size.x * size.y * size.z
	var chunks := clampi(int(vol * 300.0), 3, 14)
	_particles(chunks, 3.0, center, Vector3.UP, c, 0.0, Vector2(1, 4), 120.0, Vector2(0.04, 0.09), 1.0, 0.2)
	if material != 5:
		_particles(chunks, 2.0, center, Vector3.UP, Color(c.r, c.g, c.b, 0.45).lightened(0.2), 0.0, Vector2(0.3, 1.2), 120.0, Vector2(0.2, 0.4), 0.03, 2.0)


## Smoke grenade cloud: a volumetric fog ellipsoid that grows to the
## gadget radius, so smoke really blocks sight (like the server's smoke).
func smoke_cloud(id: int, at: Vector3, radius: float) -> void:
	var v: FogVolume = _clouds.get(id)
	if v == null:
		v = FogVolume.new()
		v.shape = RenderingServer.FOG_VOLUME_SHAPE_ELLIPSOID
		var fm := FogMaterial.new()
		fm.density = 0.0
		fm.albedo = Color(0.82, 0.82, 0.8)
		fm.height_falloff = 0.15
		fm.edge_fade = 0.6
		var noise := FastNoiseLite.new()
		noise.frequency = 0.05
		var nt := NoiseTexture3D.new()
		nt.noise = noise
		nt.width = 48
		nt.height = 48
		nt.depth = 48
		nt.seamless = true
		fm.density_texture = nt
		v.material = fm
		v.size = Vector3.ONE * 0.5
		add_child(v)
		_clouds[id] = v
		var tw := create_tween().set_parallel(true)
		tw.tween_property(v, "size", Vector3(radius * 2.2, radius * 1.6, radius * 2.2), 2.5).set_trans(Tween.TRANS_QUAD).set_ease(Tween.EASE_OUT)
		tw.tween_property(fm, "density", 6.0, 2.0)
	v.position = at + Vector3(0, radius * 0.45, 0)


func stop_smoke(id: int) -> void:
	var v: FogVolume = _clouds.get(id)
	if v == null:
		return
	_clouds.erase(id)
	var tw := create_tween()
	tw.tween_property(v.material, "density", 0.0, 3.0)
	tw.tween_callback(v.queue_free)


func active_clouds() -> Array:
	return _clouds.keys()


func _process(delta: float) -> void:
	shake = maxf(0.0, shake - delta * 2.5)
	_update_shells(delta)
