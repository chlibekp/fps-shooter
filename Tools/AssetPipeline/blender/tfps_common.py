"""Shared helpers for the TacticalFPS Blender asset pipeline.

Run asset scripts headless, e.g.:
    blender -b --factory-startup --python Tools/AssetPipeline/blender/build_crate.py

Conventions (see Documentation/ASSET_PIPELINE.md):
* 1 Blender unit = 1 meter, Z up, asset front faces -Y, pivot at floor centre.
* Outputs per asset <Name>:
    Assets/Blender/<Name>.blend                 source scene
    Assets/Exports/<Name>/SM_<Name>.fbx          LOD0 + UCX_ collision
    Assets/Exports/<Name>/SM_<Name>_LOD1.fbx     reduced LOD
    Assets/Exports/<Name>/T_<Name>_BC.png        base colour (sRGB)
    Assets/Exports/<Name>/T_<Name>_N.png         normal, DirectX (green down) for Unreal
    Assets/Exports/<Name>/T_<Name>_ORM.png       R=AO, G=roughness, B=metallic (linear)
    Assets/Exports/<Name>/preview.png            look-dev render
    Assets/Exports/<Name>/manifest.json          triangle counts, dimensions, files
"""
import json
import math
import os
import sys

import bmesh
import bpy
from mathutils import Vector

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
TEX_SIZE = int(os.environ.get("TFPS_TEX_SIZE", "512"))
BAKE_SAMPLES = int(os.environ.get("TFPS_BAKE_SAMPLES", "16"))


# ---------------------------------------------------------------------------
# Scene
# ---------------------------------------------------------------------------

def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.unit_settings.system = 'METRIC'
    sc.unit_settings.scale_length = 1.0
    sc.render.engine = 'CYCLES'
    sc.cycles.device = 'CPU'
    sc.cycles.samples = BAKE_SAMPLES
    sc.cycles.use_denoising = False
    return sc


def select_only(objs):
    bpy.ops.object.select_all(action='DESELECT')
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]


def box(name, size, location, bevel=0.0, bevel_segments=2):
    """Axis-aligned box mesh object (size and location in meters, location = centre)."""
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=location)
    o = bpy.context.active_object
    o.name = name
    o.scale = size
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if bevel > 0.0:
        m = o.modifiers.new("Bevel", 'BEVEL')
        m.width = bevel
        m.segments = bevel_segments
        m.limit_method = 'ANGLE'
        m.harden_normals = False
        apply_modifiers(o)
    return o


def apply_modifiers(o):
    select_only([o])
    for m in list(o.modifiers):
        bpy.ops.object.modifier_apply(modifier=m.name)


def join(objs, name):
    select_only(objs)
    bpy.ops.object.join()
    o = bpy.context.active_object
    o.name = name
    return o


def extrude_profile(name, profile_xz, length, bevel=0.0):
    """Extrudes a convex/concave polygon given in the XZ plane along Y (centred)."""
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    half = length / 2.0
    front = [bm.verts.new((x, -half, z)) for (x, z) in profile_xz]
    back = [bm.verts.new((x, half, z)) for (x, z) in profile_xz]
    n = len(profile_xz)
    bm.faces.new(front[::-1])
    bm.faces.new(back)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((front[i], front[j], back[j], back[i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(o)
    if bevel > 0.0:
        m = o.modifiers.new("Bevel", 'BEVEL')
        m.width = bevel
        m.segments = 2
        m.limit_method = 'ANGLE'
        apply_modifiers(o)
    return o


def shade_auto_smooth(o, angle_deg=35.0):
    select_only([o])
    bpy.ops.object.shade_smooth()
    # Blender 4.0: auto smooth lives on the mesh.
    if hasattr(o.data, "use_auto_smooth"):
        o.data.use_auto_smooth = True
        o.data.auto_smooth_angle = math.radians(angle_deg)


def uv_unwrap(o, island_margin=0.004):
    select_only([o])
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(66.0), island_margin=island_margin, scale_to_bounds=False)
    bpy.ops.uv.pack_islands(margin=island_margin)
    bpy.ops.object.mode_set(mode='OBJECT')


def triangle_count(o):
    deps = bpy.context.evaluated_depsgraph_get()
    me = o.evaluated_get(deps).to_mesh()
    me.calc_loop_triangles()
    n = len(me.loop_triangles)
    o.evaluated_get(deps).to_mesh_clear()
    return n


def dimensions(o):
    return [round(v, 4) for v in o.dimensions]


def set_origin_floor_center(o):
    """Moves geometry so the pivot is at the bottom centre of the bounds."""
    bb = [o.matrix_world @ Vector(c) for c in o.bound_box]
    mn = Vector((min(v.x for v in bb), min(v.y for v in bb), min(v.z for v in bb)))
    mx = Vector((max(v.x for v in bb), max(v.y for v in bb), max(v.z for v in bb)))
    offset = Vector(((mn.x + mx.x) / 2, (mn.y + mx.y) / 2, mn.z))
    o.data.transform(__import__("mathutils").Matrix.Translation(-offset))
    o.location = (0, 0, 0)


# ---------------------------------------------------------------------------
# Materials (procedural, baked to textures)
# ---------------------------------------------------------------------------

def new_material(name):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    nt.links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])
    coord = nt.nodes.new("ShaderNodeTexCoord")
    return m, nt, bsdf, coord


def node(nt, kind, **props):
    n = nt.nodes.new(kind)
    for k, v in props.items():
        if k.startswith("in_"):
            n.inputs[k[3:].replace("_", " ")].default_value = v
        else:
            setattr(n, k, v)
    return n


def ramp(nt, stops):
    r = nt.nodes.new("ShaderNodeValToRGB")
    els = r.color_ramp.elements
    while len(els) > 1:
        els.remove(els[-1])
    els[0].position = stops[0][0]
    els[0].color = stops[0][1]
    for pos, col in stops[1:]:
        e = els.new(pos)
        e.color = col
    return r


# ---------------------------------------------------------------------------
# Baking
# ---------------------------------------------------------------------------

def _bake_image(o, mat, img, bake_type, **kw):
    nt = mat.node_tree
    tn = nt.nodes.new("ShaderNodeTexImage")
    tn.image = img
    for n in nt.nodes:
        n.select = False
    tn.select = True
    nt.nodes.active = tn
    select_only([o])
    bpy.ops.object.bake(type=bake_type, margin=8, use_clear=True, **kw)
    nt.nodes.remove(tn)


def bake_textures(o, mat, out_dir, asset):
    """Bakes BC, normal (DirectX) and packed ORM textures for `o`."""
    os.makedirs(out_dir, exist_ok=True)
    sc = bpy.context.scene
    sc.render.bake.use_selected_to_active = False

    def img(name, non_color):
        i = bpy.data.images.new(name, TEX_SIZE, TEX_SIZE, alpha=False, float_buffer=False)
        if non_color:
            i.colorspace_settings.name = 'Non-Color'
        return i

    bc = img(f"T_{asset}_BC", False)
    _bake_image(o, mat, bc, 'DIFFUSE', pass_filter={'COLOR'})
    nrm = img(f"T_{asset}_N_bake", True)
    _bake_image(o, mat, nrm, 'NORMAL', normal_space='TANGENT')
    rough = img(f"T_{asset}_R_bake", True)
    _bake_image(o, mat, rough, 'ROUGHNESS')
    ao = img(f"T_{asset}_AO_bake", True)
    sc.cycles.samples = max(BAKE_SAMPLES, 32)
    _bake_image(o, mat, ao, 'AO')
    sc.cycles.samples = BAKE_SAMPLES

    metallic_value = mat.get("tfps_metallic", 0.0)
    metallic_map = mat.get("tfps_metallic_from_rust", False)

    # Normal: Blender bakes OpenGL (Y+); Unreal expects DirectX (Y-): flip green.
    px = list(nrm.pixels)
    for i in range(1, len(px), 4):
        px[i] = 1.0 - px[i]
    n_out = img(f"T_{asset}_N", True)
    n_out.pixels = px

    # ORM pack.
    a = list(ao.pixels)
    r = list(rough.pixels)
    orm = [0.0] * len(a)
    for i in range(0, len(a), 4):
        orm[i] = a[i]
        orm[i + 1] = r[i]
        if metallic_map:
            # Painted steel: bare metal shows where paint roughness is low.
            orm[i + 2] = 0.85 if r[i] < 0.38 else 0.0
        else:
            orm[i + 2] = metallic_value
        orm[i + 3] = 1.0
    orm_img = img(f"T_{asset}_ORM", True)
    orm_img.pixels = orm

    files = {}
    for image, suffix in ((bc, "BC"), (n_out, "N"), (orm_img, "ORM")):
        path = os.path.join(out_dir, f"T_{asset}_{suffix}.png")
        image.filepath_raw = path
        image.file_format = 'PNG'
        image.save()
        files[suffix] = os.path.relpath(path, REPO)
    return files, bc, n_out, orm_img


def make_baked_material(asset, bc, nrm, orm):
    """Simple material using the baked maps (used for the preview render and in the .blend)."""
    m, nt, bsdf, coord = new_material(f"M_{asset}")
    t_bc = node(nt, "ShaderNodeTexImage", image=bc)
    t_n = node(nt, "ShaderNodeTexImage", image=nrm)
    t_orm = node(nt, "ShaderNodeTexImage", image=orm)
    nt.links.new(t_bc.outputs["Color"], bsdf.inputs["Base Color"])
    sep = nt.nodes.new("ShaderNodeSeparateColor")
    nt.links.new(t_orm.outputs["Color"], sep.inputs["Color"])
    nt.links.new(sep.outputs["Green"], bsdf.inputs["Roughness"])
    nt.links.new(sep.outputs["Blue"], bsdf.inputs["Metallic"])
    # Flip green back for Blender's OpenGL convention.
    sep_n = nt.nodes.new("ShaderNodeSeparateColor")
    nt.links.new(t_n.outputs["Color"], sep_n.inputs["Color"])
    inv = node(nt, "ShaderNodeMath", operation='SUBTRACT', in_Value=1.0)
    inv.inputs[0].default_value = 1.0
    nt.links.new(sep_n.outputs["Green"], inv.inputs[1])
    comb = nt.nodes.new("ShaderNodeCombineColor")
    nt.links.new(sep_n.outputs["Red"], comb.inputs["Red"])
    nt.links.new(inv.outputs["Value"], comb.inputs["Green"])
    nt.links.new(sep_n.outputs["Blue"], comb.inputs["Blue"])
    nmap = nt.nodes.new("ShaderNodeNormalMap")
    nt.links.new(comb.outputs["Color"], nmap.inputs["Color"])
    nt.links.new(nmap.outputs["Normal"], bsdf.inputs["Normal"])
    return m


# ---------------------------------------------------------------------------
# LOD, collision, export, preview
# ---------------------------------------------------------------------------

def make_lod(o, ratio, name):
    select_only([o])
    bpy.ops.object.duplicate()
    lod = bpy.context.active_object
    lod.name = name
    m = lod.modifiers.new("Decimate", 'DECIMATE')
    m.ratio = ratio
    m.use_collapse_triangulate = True
    apply_modifiers(lod)
    return lod


def collision_box(mesh_name, index, size, center):
    """UCX_ convex collision box (Unreal naming convention)."""
    c = box(f"UCX_{mesh_name}_{index:02d}", size, center)
    c.display_type = 'WIRE'
    c.hide_render = True
    return c


def collision_hull(mesh_name, index, source):
    select_only([source])
    bpy.ops.object.duplicate()
    c = bpy.context.active_object
    c.name = f"UCX_{mesh_name}_{index:02d}"
    bm = bmesh.new()
    bm.from_mesh(c.data)
    res = bmesh.ops.convex_hull(bm, input=bm.verts[:])
    bmesh.ops.delete(bm, geom=res.get("geom_interior", []) + res.get("geom_unused", []), context='VERTS')
    bm.to_mesh(c.data)
    bm.free()
    c.data.materials.clear()
    c.display_type = 'WIRE'
    c.hide_render = True
    return c


def export_fbx(objs, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    select_only(objs)
    bpy.ops.export_scene.fbx(
        filepath=path,
        use_selection=True,
        apply_unit_scale=True,
        apply_scale_options='FBX_SCALE_ALL',
        axis_forward='-Z',
        axis_up='Y',
        mesh_smooth_type='FACE',
        use_tspace=True,
        add_leaf_bones=False,
        bake_anim=False,
        path_mode='STRIP',
    )
    return os.path.relpath(path, REPO)


def render_preview(o, path, distance_scale=3.3):
    sc = bpy.context.scene
    dims = o.dimensions
    radius = dims.length * 0.5
    center = Vector((0, 0, dims.z * 0.45))
    # Ground plane, camera, lights.
    bpy.ops.mesh.primitive_plane_add(size=max(dims) * 8, location=(0, 0, 0))
    ground = bpy.context.active_object
    gm, gnt, gb, _ = new_material("M_PreviewGround")
    gb.inputs["Base Color"].default_value = (0.18, 0.18, 0.18, 1.0)
    gb.inputs["Roughness"].default_value = 0.9
    ground.data.materials.append(gm)
    cam_data = bpy.data.cameras.new("PreviewCam")
    cam_data.lens = 50
    cam = bpy.data.objects.new("PreviewCam", cam_data)
    sc.collection.objects.link(cam)
    d = radius * distance_scale
    cam.location = center + Vector((d * 0.55, -d * 0.75, d * 0.35))
    direction = center - cam.location
    cam.rotation_euler = direction.to_track_quat('-Z', 'Y').to_euler()
    sc.camera = cam
    sun_data = bpy.data.lights.new("Sun", 'SUN')
    sun_data.energy = 2.6
    sun_data.angle = math.radians(3)
    sun = bpy.data.objects.new("Sun", sun_data)
    sun.rotation_euler = (math.radians(50), math.radians(10), math.radians(35))
    sc.collection.objects.link(sun)
    world = bpy.data.worlds.new("PreviewWorld")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.45, 0.55, 0.7, 1.0)
    world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.6
    sc.world = world
    sc.render.resolution_x = 768
    sc.render.resolution_y = 768
    sc.cycles.samples = 96
    sc.cycles.use_denoising = False  # distro Blender builds may lack OpenImageDenoise
    sc.view_settings.view_transform = 'Filmic' if 'Filmic' in [v.identifier for v in type(sc.view_settings).bl_rna.properties['view_transform'].enum_items] else 'Standard'
    sc.render.filepath = path
    sc.render.image_settings.file_format = 'PNG'
    bpy.ops.render.render(write_still=True)
    for obj in (ground, cam, sun):
        bpy.data.objects.remove(obj, do_unlink=True)
    sc.cycles.samples = BAKE_SAMPLES
    return os.path.relpath(path, REPO)


def finish_asset(asset, lod0, lod1, collisions, procedural_mat, notes=""):
    """Bakes, exports, previews and writes the manifest for one asset."""
    out_dir = os.path.join(REPO, "Assets", "Exports", asset)
    os.makedirs(out_dir, exist_ok=True)
    files, bc, nrm, orm = bake_textures(lod0, procedural_mat, out_dir, asset)
    baked = make_baked_material(asset, bc, nrm, orm)
    for o in (lod0, lod1):
        o.data.materials.clear()
        o.data.materials.append(baked)
    mesh_name = f"SM_{asset}"
    lod0.name = mesh_name
    lod1.name = f"{mesh_name}_LOD1"
    files["fbx_lod0"] = export_fbx([lod0] + collisions, os.path.join(out_dir, f"{mesh_name}.fbx"))
    files["fbx_lod1"] = export_fbx([lod1], os.path.join(out_dir, f"{mesh_name}_LOD1.fbx"))
    lod1.hide_render = True
    for c in collisions:
        c.hide_render = True
    files["preview"] = render_preview(lod0, os.path.join(out_dir, "preview.png"))
    blend_path = os.path.join(REPO, "Assets", "Blender", f"{asset}.blend")
    os.makedirs(os.path.dirname(blend_path), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=blend_path, compress=True)
    # Store texture paths relative to the .blend so the repo can live anywhere.
    bpy.ops.file.make_paths_relative()
    bpy.ops.wm.save_as_mainfile(filepath=blend_path, compress=True)
    files["blend"] = os.path.relpath(blend_path, REPO)
    manifest = {
        "asset": asset,
        "unreal_mesh": mesh_name,
        "dimensions_m": dimensions(lod0),
        "triangles_lod0": triangle_count(lod0),
        "triangles_lod1": triangle_count(lod1),
        "collision_primitives": len(collisions),
        "texture_size": TEX_SIZE,
        "files": files,
        "notes": notes,
    }
    with open(os.path.join(out_dir, "manifest.json"), "w", newline="\n") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    print("TFPS_ASSET_DONE", json.dumps(manifest))
    return manifest
