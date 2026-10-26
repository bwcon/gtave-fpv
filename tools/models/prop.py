"""Builds a single-texture prop (the pilot's goggles, the radio) from a Sketchfab GLB:
reduced, scaled to real size, turned so its front faces +Y, with its colour texture.

Needs Blender 5.1 with Sollumz 2.9, and the colour texture as a DDS (make_dds.py):
    blender --background --python tools/models/prop.py -- <model.glb> <colour.dds> <out dir> <name> <width m> <turn deg> <triangles>
    blender --background --python tools/models/prop.py -- <model.glb> --texture <out.png>   (the GLB's colour texture)
Then export_model.py with the same name.

  goggles: "Tripo FPV DJI Goggles" by line-o, CC BY 4.0, gtavefpv_goggles 0.22 90 8000
    https://sketchfab.com/3d-models/tripo-fpv-dji-goggles-9edb8544e5ba4cd1985fd997d366b260
  radio: "Radiomaster TX12 rc radio controller" by flightclubua, CC BY 4.0, gtavefpv_radio 0.175 0 8000
    https://sketchfab.com/3d-models/radiomaster-tx12-rc-radio-controller-0edc6bca739d4aa6952bb6b8747318bb
"""
import math
import os
import sys

import bpy
import mathutils

args = sys.argv[sys.argv.index("--") + 1:]
bpy.ops.wm.read_homefile(use_empty=True)
bpy.ops.import_scene.gltf(filepath=args[0])
if args[1] == "--texture":  # save the material's base colour image
    mat = next(m for m in bpy.data.materials if m.use_nodes)
    bsdf = next(n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    image = bsdf.inputs["Base Color"].links[0].from_node.image
    image.filepath_raw = args[2]
    image.file_format = "PNG"
    image.save()
    print("TEXTURE", image.name, tuple(image.size), args[2])
    sys.exit(0)
glb, dds, out, NAME = args[:4]
WIDTH, TURN, TARGET_TRIS = float(args[4]), float(args[5]), int(args[6])
os.makedirs(out, exist_ok=True)
meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
bpy.ops.object.select_all(action="DESELECT")
for o in meshes:
    o.select_set(True)
bpy.context.view_layer.objects.active = meshes[0]
bpy.ops.object.parent_clear(type="CLEAR_KEEP_TRANSFORM")
if len(meshes) > 1:
    bpy.ops.object.join()
obj = bpy.context.view_layer.objects.active
obj.name = obj.data.name = NAME
for o in [o for o in bpy.context.scene.objects if o is not obj]:
    bpy.data.objects.remove(o, do_unlink=True)

# On the mesh data itself (transform_apply does nothing in background mode):
# world transform in, turned so the front faces +Y, real size (WIDTH across X), centred.
me = obj.data
me.transform(obj.matrix_world)
obj.matrix_world = mathutils.Matrix.Identity(4)
me.transform(mathutils.Matrix.Rotation(math.radians(TURN), 4, "Z"))
xs = [v.co.x for v in me.vertices]
me.transform(mathutils.Matrix.Scale(WIDTH / (max(xs) - min(xs)), 4))
lo = mathutils.Vector([min(v.co[i] for v in me.vertices) for i in range(3)])
hi = mathutils.Vector([max(v.co[i] for v in me.vertices) for i in range(3)])
me.transform(mathutils.Matrix.Translation(-(lo + hi) / 2))
print("SIZE", [round(d, 3) for d in obj.dimensions])

dec = obj.modifiers.new("decimate", "DECIMATE")
dec.ratio = min(1.0, TARGET_TRIS / sum(len(p.vertices) - 2 for p in me.polygons))
bpy.context.view_layer.objects.active = obj
bpy.ops.object.modifier_apply(modifier=dec.name)
print("TRIS", sum(len(p.vertices) - 2 for p in me.polygons))

# Sollumz's default shader: "UVMap 0" and white "Color 0"/"Color 1".
while len(me.uv_layers) > 1:
    me.uv_layers.remove(me.uv_layers[-1])
me.uv_layers[0].name = "UVMap 0"
for cname in ("Color 0", "Color 1"):
    ca = me.color_attributes.get(cname) or me.color_attributes.new(cname, "BYTE_COLOR", "CORNER")
    ca.data.foreach_set("color", [1.0] * (4 * len(ca.data)))

# One material with the colour texture (a DDS: Sollumz only embeds DDS).
img = bpy.data.images.load(dds)
img.name = NAME
img.pack()
mat = bpy.data.materials.new(NAME)
mat.use_nodes = True
tex = mat.node_tree.nodes.new("ShaderNodeTexImage")
tex.image = img
mat.node_tree.links.new(tex.outputs["Color"], mat.node_tree.nodes["Principled BSDF"].inputs["Base Color"])
me.materials.clear()
me.materials.append(mat)

bpy.ops.object.select_all(action="DESELECT")
obj.select_set(True)
bpy.context.view_layer.objects.active = obj
print("CONVERT", bpy.ops.sollumz.autoconvertmaterials())
print("DRAWABLE", bpy.ops.sollumz.converttodrawable())
drawable = next(o for o in bpy.context.scene.objects if getattr(o, "sollum_type", "") == "sollumz_drawable")
drawable.name = NAME
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(out, NAME + ".blend"))
print("SAVED")
