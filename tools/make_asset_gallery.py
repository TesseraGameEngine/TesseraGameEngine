"""Builds projects/DefaultProject/scenes/asset_gallery.scene: the optimized Poly Haven models
side by side at real scale (the big fir at its three levels of detail, other trees, rocks, plants,
camp props) on a ground plane, for checking materials, cut-outs, scale and LODs.

Needs tools/fetch_assets.py and Tessera --optimize-models first.
Run: Tessera --game --scene=projects/DefaultProject/scenes/asset_gallery.scene
"""
import copy
import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEMPLATE = os.path.join(ROOT, "projects", "DefaultProject", "scenes", "main.scene")
OUT = os.path.join(ROOT, "projects", "DefaultProject", "scenes", "asset_gallery.scene")
MODELS = "assets/external/polyhaven/models"

base = json.load(open(TEMPLATE, encoding="utf-8"))
mesh_t = next(n for n in base["nodes"] if n["nodeType"] == "Mesh")
light_t = next(n for n in base["nodes"] if n["nodeType"] == "Light")
cam_t = next(n for n in base["nodes"] if n["nodeType"] == "Camera")
nodes = []


def add(template, **fields):
    n = copy.deepcopy(template)
    n["id"] = len(nodes) + 1
    n["parentId"] = 0
    n["children"] = []
    t = fields.pop("transform", None)
    if t:
        n["transform"] = {"position": t[0], "rotation": t[1], "scale": t[2]}
    n.update(fields)
    nodes.append(n)
    return n


def model(asset, variant, x, z, lod=0, rot=0.0):
    suffix = f"_{variant}" if variant else ""
    path = f"{MODELS}/{asset}/{asset}_1k{suffix}_lod{lod}.gltf"
    if not os.path.exists(os.path.join(ROOT, path)):
        raise SystemExit("missing " + path + " (run fetch_assets.py and --optimize-models)")
    return add(mesh_t, name=f"{asset}{suffix}_lod{lod}", shapeType="Custom", customMeshName=path,
               color=[1, 1, 1, 1], roughness=1.0, metallic=0.0, doubleSided=True,
               transform=([x, 0, z], [0, rot, 0], [1, 1, 1]))


add(mesh_t, name="Ground", shapeType="Plane", color=[0.32, 0.30, 0.25, 1], roughness=0.95,
    transform=([0, 0, -10], [0, 0, 0], [40, 1, 40]))
# The big fir at LOD 0 / 1 / 2 (each a separate file, same size)
for i, lod in enumerate([0, 1, 2]):
    model("fir_tree_01", "a", -14 + i * 9, -18, lod)
model("fir_sapling_medium", "a", 14, -16)
model("island_tree_01", "", 22, -8)
model("tree_small_02", "", -24, -6)
model("coast_rocks_05", "", -10, -2)
model("rock_moss_set_01", "rock01", -3, 2, rot=40)
model("shrub_02", "a", 3, 1)
model("fern_02", "b", 5.5, 3)
model("grass_medium_02", "e", 7, 4)
model("wooden_picnic_table", "", 11, 0, rot=20)
model("barrel_03", "", 13.5, 3)
model("wooden_lantern_01", "", 11, 0.2).update({"transform": {"position": [11, 0.75, 0.2], "rotation": [0, 0, 0], "scale": [1, 1, 1]}})
model("wooden_crate_02", "", 9, 4)

sun = add(light_t, name="Sun", transform=([0, 30, 0], [-32, 140, 0], [1, 1, 1]))
sun["light"] = dict(sun["light"], type="Directional", intensity=3.2, color=[1.0, 0.94, 0.85], castShadows=True)
cam = add(cam_t, name="Camera", transform=([0, 6.5, 16], [-10, 0, 0], [1, 1, 1]))

env = copy.deepcopy(base["environment"])
env["enableSky"] = True
env["shadowDistance"] = 80
scene = {"activeCameraId": cam["id"], "environment": env, "name": "asset_gallery", "nextId": len(nodes) + 1, "nodes": nodes}
json.dump(scene, open(OUT, "w", encoding="utf-8"), indent=1)
print("wrote", OUT)
