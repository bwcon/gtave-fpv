"""Builds the GTA V drone prop from the DJI Avata2 Sketchfab model.

DJI Avata2 by raphael.harris.gaffga, CC BY 4.0:
https://sketchfab.com/3d-models/dji-avata2-e27ed758e2174a89a48368e84027f8d9
Reduced (decimated), re-oriented and its materials baked for GTA V.

Needs Blender 5.1 with Sollumz 2.9 (and its szio and PyMateria packages):
    blender --background --python tools/models/avata2.py -- <model.glb> <out dir>
Writes <out dir>/gtavefpv_avata2.ydr and gtavefpv_avata2.ytyp (GEN9, for GTA V Enhanced),
plus <out dir>/build.blend for inspection.
"""
import math
import os
import sys

import bpy
import mathutils

NAME = "gtavefpv_avata2"
TARGET_TRIS = 15000
TEXTURE_SIZE = "1024"

glb, out = sys.argv[sys.argv.index("--") + 1:][:2]
os.makedirs(out, exist_ok=True)


def tris(obj):
    return sum(len(p.vertices) - 2 for p in obj.data.polygons)


def select_only(obj):
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj


bpy.ops.wm.read_homefile(use_empty=True)
bpy.ops.import_scene.gltf(filepath=glb)

# One mesh: GTA draws a prop as one drawable, and one baked texture is cheaper than 11 materials.
meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
bpy.ops.object.select_all(action="DESELECT")
for o in meshes:
    o.select_set(True)
bpy.context.view_layer.objects.active = meshes[0]
# the importer's root node holds the mm -> m scale: keep it when unparenting
bpy.ops.object.parent_clear(type="CLEAR_KEEP_TRANSFORM")
bpy.ops.object.join()
obj = bpy.context.view_layer.objects.active
obj.name = obj.data.name = NAME
for o in [o for o in bpy.context.scene.objects if o is not obj]:
    bpy.data.objects.remove(o, do_unlink=True)
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

# The nose (the camera lens) points to -Y in the file; GTA's forward is +Y. Turned on the mesh
# data itself, then centred on its bounds (the origin is where the mod places the drone).
obj.data.transform(mathutils.Matrix.Rotation(math.pi, 4, "Z"))
lo = mathutils.Vector([min(v.co[i] for v in obj.data.vertices) for i in range(3)])
hi = mathutils.Vector([max(v.co[i] for v in obj.data.vertices) for i in range(3)])
obj.data.transform(mathutils.Matrix.Translation(-(lo + hi) / 2))
obj.location = (0, 0, 0)
print("SIZE", [round(d, 3) for d in obj.dimensions], "TRIS", tris(obj))

# ~15k triangles: the prop is a few hundred pixels at most, even in the chase view.
dec = obj.modifiers.new("decimate", "DECIMATE")
dec.ratio = min(1.0, TARGET_TRIS / tris(obj))
bpy.ops.object.modifier_apply(modifier=dec.name)
print("DECIMATED", tris(obj))

# Sollumz's default shader reads "UVMap 0" and the colour layers "Color 0" and "Color 1"
# (white: the flat texture colours show as they are).
me = obj.data
while len(me.uv_layers) > 1:
    me.uv_layers.remove(me.uv_layers[-1])
if me.uv_layers:
    me.uv_layers[0].name = "UVMap 0"
else:
    me.uv_layers.new(name="UVMap 0")
for cname in ("Color 0", "Color 1"):
    ca = me.color_attributes.get(cname) or me.color_attributes.new(cname, "BYTE_COLOR", "CORNER")
    ca.data.foreach_set("color", [1.0] * (4 * len(ca.data)))
print("LAYERS", [u.name for u in me.uv_layers], [c.name for c in me.color_attributes])

# Flat colours, one per material group: a decimated mesh unwraps into thousands of tiny
# UV islands, and a baked atlas of them bleeds into the background in GTA's mipmaps.
# Each colour is the material's average texture colour (or its base colour), in sRGB.
def to_srgb(c):
    return 12.92 * c if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055


def write_dds(path, rgb, size=4):
    """An uncompressed 32-bit DDS (A8R8G8B8) filled with one sRGB colour: Sollumz only embeds DDS."""
    import struct
    pixel = bytes([round(rgb[2] * 255), round(rgb[1] * 255), round(rgb[0] * 255), 255])  # B G R A
    header = struct.pack("<4s7I44x", b"DDS ", 124, 0x100F, size, size, size * 4, 0, 1)
    pf = struct.pack("<8I", 32, 0x41, 0, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    caps = struct.pack("<5I", 0x1000, 0, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(header + pf + caps + pixel * (size * size))


def material_colour(m):
    bsdf = next((n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED"), None) if m.use_nodes else None
    if not bsdf:
        return (0.2, 0.2, 0.2)
    src = bsdf.inputs["Base Color"]
    image = None
    if src.is_linked:
        stack = [src.links[0].from_node]
        while stack and not image:  # through mix and colour nodes to the first image
            node = stack.pop()
            if node.type == "TEX_IMAGE" and node.image and node.image.size[0]:
                image = node.image
            stack += [l.from_node for i in node.inputs for l in i.links]
    if image:
        px = image.pixels[:]
        n = len(px) // 4
        return tuple(sum(px[c::4]) / n for c in range(3))  # byte images hold their sRGB values
    return tuple(to_srgb(c) for c in src.default_value[:3])


# Where a texture is not the colour: the glass's is a roughness map, the metal's a scratch overlay.
OVERRIDES = {
    "Glass": (0.04, 0.05, 0.07),
    "Scratched Black Metal": (0.14, 0.14, 0.15),
    "Procedural pearl black painted plastic": (0.08, 0.08, 0.09),
}
groups = {}  # rounded colour -> new material
slot_map = []
for slot in obj.material_slots:
    name = slot.material.name if slot.material else ""
    col = OVERRIDES.get(name) or (material_colour(slot.material) if slot.material else (0.2, 0.2, 0.2))
    col = tuple(max(c, 0.1) for c in col) if name not in OVERRIDES else col  # near-black loses its shape in game
    key = tuple(round(c * 20) / 20 for c in col)
    if key not in groups:
        i = len(groups)
        dds = os.path.join(out, f"{NAME}_{i}.dds")
        write_dds(dds, col)
        img = bpy.data.images.load(dds)
        img.name = f"{NAME}_{i}"
        img.pack()
        mat = bpy.data.materials.new(f"{NAME}_{i}")
        mat.use_nodes = True
        bsdf = mat.node_tree.nodes["Principled BSDF"]
        tex = mat.node_tree.nodes.new("ShaderNodeTexImage")
        tex.image = img
        mat.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
        groups[key] = mat
        print("COLOUR", mat.name, [round(c, 2) for c in col], "from", slot.material.name if slot.material else None)
    slot_map.append(groups[key])
# One slot per group: faces move to their group's first slot, the empty slots go.
first = {}
remap = [first.setdefault(mat.name, i) for i, mat in enumerate(slot_map)]
for p in obj.data.polygons:
    p.material_index = remap[p.material_index]
for slot, mat in zip(obj.material_slots, slot_map):
    slot.material = mat
select_only(obj)
bpy.ops.object.material_slot_remove_unused()
print("SLOTS", [s.material.name for s in obj.material_slots])

r = bpy.ops.sollumz.autoconvertmaterials()
print("CONVERT", r, [(m.name, getattr(m, "shader_properties", None) and m.shader_properties.filename) for m in obj.data.materials if m])

select_only(obj)
r = bpy.ops.sollumz.converttodrawable()
drawable = next(o for o in bpy.context.scene.objects if getattr(o, "sollum_type", "") == "sollumz_drawable")
drawable.name = NAME
print("DRAWABLE", r, drawable.name, drawable.type, [(c.name, c.sollum_type) for c in drawable.children])
print("SIZE_M", [round(d, 3) for d in obj.dimensions], "TRIS", tris(obj))

bpy.ops.wm.save_as_mainfile(filepath=os.path.join(out, "build.blend"))
print("SAVED")
