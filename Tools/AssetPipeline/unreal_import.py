"""Imports the Blender pipeline's exports into the Unreal project.

Run inside the Unreal Editor (Python Editor Script Plugin enabled):

    Tools > Execute Python Script... > Tools/AssetPipeline/unreal_import.py

or headless:

    UnrealEditor-Cmd Client/TacticalFPS.uproject -run=pythonscript -script="<repo>/Tools/AssetPipeline/unreal_import.py"

For every Assets/Exports/<Asset>/manifest.json it:
  1. imports SM_<Asset>.fbx into /Game/Environment/Props/<Asset> (UCX_
     collision is picked up automatically; meshes are not combined);
  2. imports SM_<Asset>_LOD1.fbx into the mesh's LOD 1 slot;
  3. imports the BC / N / ORM textures with the right compression
     (BC sRGB, N normal map, ORM masks without sRGB);
  4. creates MI_<Asset>, a material instance of /Game/Materials/M_TfpsMaster
     if that parent exists. Otherwise it creates a small standalone material
     wiring the three maps.

Status: written against the documented UE 5 Python API but NOT yet run in
an editor (the development machine has no Unreal Engine). See
Documentation/STATUS.md.
"""
import json
import os

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
EXPORTS = os.path.join(REPO, "Assets", "Exports")
DEST_ROOT = "/Game/Environment/Props"
MASTER = "/Game/Materials/M_TfpsMaster"

tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary


def import_task(filename, dest, options=None):
    task = unreal.AssetImportTask()
    task.filename = filename
    task.destination_path = dest
    task.automated = True
    task.replace_existing = True
    task.save = True
    if options is not None:
        task.options = options
    tools.import_asset_tasks([task])
    return [eal.load_asset(p) for p in task.imported_object_paths]


def fbx_options():
    o = unreal.FbxImportUI()
    o.import_mesh = True
    o.import_materials = False
    o.import_textures = False
    o.import_as_skeletal = False
    o.static_mesh_import_data.combine_meshes = False
    o.static_mesh_import_data.auto_generate_collision = False
    o.static_mesh_import_data.import_uniform_scale = 1.0
    return o


def import_texture(path, dest, kind):
    tex = import_task(path, dest)[0]
    if kind == "N":
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property("srgb", False)
    elif kind == "ORM":
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
        tex.set_editor_property("srgb", False)
    eal.save_loaded_asset(tex)
    return tex


def standalone_material(asset, dest, bc, n, orm):
    mel = unreal.MaterialEditingLibrary
    mat = tools.create_asset(f"M_{asset}", dest, unreal.Material, unreal.MaterialFactoryNew())
    def sample(tex, x, y, sampler):
        s = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSample, x, y)
        s.set_editor_property("texture", tex)
        s.set_editor_property("sampler_type", sampler)
        return s
    s_bc = sample(bc, -600, -200, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    s_n = sample(n, -600, 100, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    s_orm = sample(orm, -600, 400, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    mel.connect_material_property(s_bc, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(s_n, "RGB", unreal.MaterialProperty.MP_NORMAL)
    mel.connect_material_property(s_orm, "R", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    mel.connect_material_property(s_orm, "G", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(s_orm, "B", unreal.MaterialProperty.MP_METALLIC)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    return mat


def material_instance(asset, dest, bc, n, orm):
    parent = eal.load_asset(MASTER) if eal.does_asset_exist(MASTER) else None
    if parent is None:
        return standalone_material(asset, dest, bc, n, orm)
    mi = tools.create_asset(f"MI_{asset}", dest, unreal.MaterialInstanceConstant,
                            unreal.MaterialInstanceConstantFactoryNew())
    mel = unreal.MaterialEditingLibrary
    mel.set_material_instance_parent(mi, parent)
    mel.set_material_instance_texture_parameter_value(mi, "BaseColor", bc)
    mel.set_material_instance_texture_parameter_value(mi, "Normal", n)
    mel.set_material_instance_texture_parameter_value(mi, "ORM", orm)
    eal.save_loaded_asset(mi)
    return mi


def import_asset(manifest_path):
    with open(manifest_path) as f:
        man = json.load(f)
    asset = man["asset"]
    dest = f"{DEST_ROOT}/{asset}"
    files = {k: os.path.join(REPO, v) for k, v in man["files"].items()}
    meshes = [o for o in import_task(files["fbx_lod0"], dest, fbx_options()) if isinstance(o, unreal.StaticMesh)]
    if not meshes:
        unreal.log_error(f"{asset}: no static mesh imported")
        return
    mesh = meshes[0]
    unreal.EditorStaticMeshLibrary.import_lod(mesh, 1, files["fbx_lod1"])
    bc = import_texture(files["BC"], dest, "BC")
    n = import_texture(files["N"], dest, "N")
    orm = import_texture(files["ORM"], dest, "ORM")
    mat = material_instance(asset, dest, bc, n, orm)
    mesh.set_material(0, mat)
    eal.save_loaded_asset(mesh)
    unreal.log(f"Imported {asset}: {man['triangles_lod0']} tris, LOD1 {man['triangles_lod1']}")


def main():
    for name in sorted(os.listdir(EXPORTS)):
        path = os.path.join(EXPORTS, name, "manifest.json")
        if os.path.isfile(path):
            import_asset(path)


main()
