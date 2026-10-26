"""Exports the built Avata2 drawable (build_model.py's build.blend) and its .ytyp.

    blender --background <out>/build.blend --python tools/avata2/export_model.py -- <out>
Writes <out>/gen9/ and <out>/gen8/ with gtavefpv_avata2.ydr and gtavefpv_avata2.ytyp.
"""
import os
import sys

import bpy

NAME = "gtavefpv_avata2"
out = sys.argv[sys.argv.index("--") + 1]

drawable = bpy.data.objects[NAME]
bpy.ops.object.select_all(action="DESELECT")
drawable.select_set(True)
bpy.context.view_layer.objects.active = drawable

# The archetype the game spawns by name: a small dynamic prop, seen from up to 150 m.
r = bpy.ops.sollumz.createytyp()
ytyp = bpy.context.scene.ytyps[bpy.context.scene.ytyp_index]
ytyp.name = NAME
r2 = bpy.ops.sollumz.createarchetypefromselected()
arch = ytyp.archetypes[len(ytyp.archetypes) - 1]
arch.name = NAME
arch.lod_dist = 150.0
if hasattr(arch, "texture_dictionary"):
    arch.texture_dictionary = NAME  # textures are embedded in the .ydr
# The game culls by these: from the mesh itself (Sollumz left them at 0).
model = next(c for c in drawable.children if c.type == "MESH")
lo = [min(v.co[i] for v in model.data.vertices) for i in range(3)]
hi = [max(v.co[i] for v in model.data.vertices) for i in range(3)]
arch.bb_min, arch.bb_max = lo, hi
arch.bs_center = [(a + b) / 2 for a, b in zip(lo, hi)]
arch.bs_radius = sum(((b - a) / 2) ** 2 for a, b in zip(lo, hi)) ** 0.5
print("YTYP", r, r2, ytyp.name, len(ytyp.archetypes), arch.name, arch.type if hasattr(arch, "type") else "",
      "bb", [round(v, 3) for v in arch.bb_min], [round(v, 3) for v in arch.bb_max], "radius", round(arch.bs_radius, 3))

# The texture dictionary the archetype names: the drawable's images, from its DDS files.
bpy.ops.sollumz.txd_create()
txds = bpy.context.scene.sz_txds.texture_dictionaries_
td = txds[len(txds) - 1]
td.name = NAME
bpy.ops.sollumz.txd_create_source()
src = td.sources_[0]
src.source_type = "OBJECT"
src.object_name = NAME
src.object_include_children = True
bpy.ops.sollumz.txd_refresh_sources()
print("TXD", td.name, [t.name for t in td.textures_])

for gen in ("GEN9", "GEN8"):
    d = os.path.join(out, gen.lower())
    os.makedirs(d, exist_ok=True)
    common = dict(directory=d, use_custom_settings=True, target_formats={"NATIVE"}, target_versions={gen},
                  limit_to_selected=False, direct_export=True)
    r = bpy.ops.sollumz.export_assets(apply_transforms=False, export_ytyps=True, export_ytyps_include="ALL", **common)
    r2 = bpy.ops.sollumz.export_ytd(export_ytds=True, export_ytds_include="ALL", **common)
    print("EXPORT", gen, r, r2, {f: os.path.getsize(os.path.join(d, f)) for f in sorted(os.listdir(d))})
