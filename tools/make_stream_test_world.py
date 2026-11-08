"""Builds a synthetic streaming test world in projects/DefaultProject/scenes/streamtest/:
an 8 x 8 grid of 64 m sectors, each a field of coloured pillars (a colour per sector), with a
grey low-detail plate per sector in the always-loaded base scene. A script flies the camera
across the grid so sectors stream in ahead of it and out behind it.

Usage: python tools/make_stream_test_world.py
Run:   Tessera --game --scene=projects/DefaultProject/scenes/streamtest/streamtest.world
"""
import colorsys
import copy
import json
import os
import random

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEMPLATE = os.path.join(ROOT, "projects", "DefaultProject", "scenes", "main.scene")
OUT = os.path.join(ROOT, "projects", "DefaultProject", "scenes", "streamtest")
GRID = 8
CELL = 64.0

base_json = json.load(open(TEMPLATE, encoding="utf-8"))
mesh_t = next(n for n in base_json["nodes"] if n["nodeType"] == "Mesh")
light_t = next(n for n in base_json["nodes"] if n["nodeType"] == "Light")
cam_t = next(n for n in base_json["nodes"] if n["nodeType"] == "Camera")


def node(template, nodes, **fields):
    n = copy.deepcopy(template)
    n["id"] = len(nodes) + 1
    n["parentId"] = fields.pop("parentId", 0)
    n["children"] = []
    t = fields.pop("transform", None)
    if t:
        n["transform"] = {"position": t[0], "rotation": t[1], "scale": t[2]}
    n.update(fields)
    nodes.append(n)
    return n


def scene(name, nodes, camera_id=0, env=None):
    return {"activeCameraId": camera_id, "environment": env or copy.deepcopy(base_json["environment"]),
            "name": name, "nextId": len(nodes) + 1, "nodes": nodes}


os.makedirs(os.path.join(OUT, "sectors"), exist_ok=True)
rng = random.Random(7)
sectors = []
for gx in range(GRID):
    for gz in range(GRID):
        nodes = []
        hue = ((gx * GRID + gz) * 0.618) % 1.0
        r, g, b = colorsys.hsv_to_rgb(hue, 0.65, 0.9)
        x0, z0 = gx * CELL, gz * CELL
        # Ground plate for the detailed sector (slightly above the base plate it replaces)
        node(mesh_t, nodes, name="Ground", shapeType="Plane", color=[0.42, 0.45, 0.4, 1], roughness=0.9,
             transform=([x0 + CELL / 2, 0.0, z0 + CELL / 2], [0, 0, 0], [CELL / 2, 1, CELL / 2]))
        for i in range(40):
            h = rng.uniform(1.0, 7.0)
            node(mesh_t, nodes, name=f"Pillar{i}", shapeType=rng.choice(["Cube", "Cylinder"]),
                 color=[r, g, b, 1], roughness=rng.uniform(0.15, 0.7), metallic=rng.choice([0.0, 0.0, 1.0]),
                 transform=([x0 + rng.uniform(3, CELL - 3), h / 2, z0 + rng.uniform(3, CELL - 3)],
                            [0, rng.uniform(0, 90), 0], [rng.uniform(0.6, 1.6), h, rng.uniform(0.6, 1.6)]))
        rel = f"sectors/sector_{gx}_{gz}.scene"
        path = os.path.join(OUT, rel)
        json.dump(scene(f"sector_{gx}_{gz}", nodes), open(path, "w", encoding="utf-8"), indent=1)
        sectors.append({"x": gx, "z": gz, "file": rel, "lodNode": f"LOD_{gx}_{gz}", "bytes": os.path.getsize(path)})

# Base scene: sun, camera (with the fly-over script) and a grey stand-in plate per sector
base_nodes = []
sun = node(light_t, base_nodes, name="Sun", transform=([0, 50, 0], [-40, 30, 0], [1, 1, 1]))
sun["light"] = dict(sun["light"], type="Directional", intensity=3.0, castShadows=True)
cam = node(cam_t, base_nodes, name="Main Camera", scriptPath="scripts/StreamFlyover.lua",
           transform=([20.0, 14.0, 20.0], [-18, -135, 0], [1, 1, 1]))
cam["camera"] = dict(cam["camera"], far=2000)
for s in sectors:
    node(mesh_t, base_nodes, name=s["lodNode"], shapeType="Plane", color=[0.3, 0.3, 0.32, 1], roughness=0.95,
         transform=([s["x"] * CELL + CELL / 2, -0.05, s["z"] * CELL + CELL / 2], [0, 0, 0], [CELL / 2, 1, CELL / 2]))
env = copy.deepcopy(base_json["environment"])
env["enableSky"] = True
json.dump(scene("base", base_nodes, cam["id"], env), open(os.path.join(OUT, "base.scene"), "w", encoding="utf-8"), indent=1)

world = {"format": "tessera-world-1", "name": "StreamTest", "cellSize": CELL, "baseScene": "base.scene",
         "loadRadius": 96, "unloadRadius": 140, "sectors": sectors}
json.dump(world, open(os.path.join(OUT, "streamtest.world"), "w", encoding="utf-8"), indent=1)
print("wrote", len(sectors), "sectors to", OUT)
