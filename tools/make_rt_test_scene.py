"""Builds projects/DefaultProject/scenes/rt_test.scene: a small scene for checking hardware ray
tracing (soft sun shadows, contact shadows, reflections of off-screen objects).

Usage: python tools/make_rt_test_scene.py
It copies node templates from the project's main.scene so the format always matches the engine.
"""
import copy
import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "projects", "DefaultProject", "scenes", "main.scene")
DST = os.path.join(ROOT, "projects", "DefaultProject", "scenes", "rt_test.scene")

base = json.load(open(SRC, encoding="utf-8"))
mesh_t = next(n for n in base["nodes"] if n["nodeType"] == "Mesh")
light_t = next(n for n in base["nodes"] if n["nodeType"] == "Light")
cam_t = next(n for n in base["nodes"] if n["nodeType"] == "Camera")

nodes = []


def add(template, **fields):
    n = copy.deepcopy(template)
    n["id"] = len(nodes) + 1
    n["parentId"] = 0
    n["children"] = []
    transform = fields.pop("transform", None)
    if transform:
        n["transform"] = {"position": transform[0], "rotation": transform[1], "scale": transform[2]}
    n.update(fields)
    nodes.append(n)
    return n


# Ground: matte, large enough that shadows land on it
add(mesh_t, name="Ground", shapeType="Plane", color=[0.55, 0.55, 0.52, 1], roughness=0.85,
    transform=([0, 0, 0], [0, 0, 0], [15, 1, 15]))
# A glossy dark tile in front of the camera: shows reflections of the objects (and the tall
# pillar behind the camera, which screen-space reflections cannot see)
add(mesh_t, name="Glossy Floor", shapeType="Plane", color=[0.05, 0.05, 0.06, 1], roughness=0.05, metallic=0.0,
    transform=([0, 0.01, 2.0], [0, 0, 0], [3, 1, 2]))
add(mesh_t, name="Chrome Sphere", shapeType="Sphere", color=[0.95, 0.93, 0.88, 1], roughness=0.04, metallic=1.0,
    transform=([0, 1.0, 0], [0, 0, 0], [2, 2, 2]))
add(mesh_t, name="Red Cube", shapeType="Cube", color=[0.75, 0.12, 0.1, 1], roughness=0.5,
    transform=([-2.6, 0.6, -0.8], [0, 30, 0], [1.2, 1.2, 1.2]))
add(mesh_t, name="Green Cone", shapeType="Cone", color=[0.15, 0.6, 0.25, 1], roughness=0.6,
    transform=([2.6, 0.75, -0.6], [0, 0, 0], [1.2, 1.5, 1.2]))
add(mesh_t, name="Tall Pillar", shapeType="Cylinder", color=[0.9, 0.85, 0.7, 1], roughness=0.4,
    transform=([1.5, 3.0, -6.0], [0, 0, 0], [0.8, 6, 0.8]))
# Behind the camera: only ray-traced reflections can show it in the chrome sphere
add(mesh_t, name="Blue Wall Behind Camera", shapeType="Cube", color=[0.1, 0.25, 0.85, 1], roughness=0.5,
    transform=([0, 2.0, 12.0], [0, 0, 0], [8, 4, 0.4]))

sun = add(light_t, name="Sun", transform=([0, 10, 0], [-35, 35, 0], [1, 1, 1]))
sun["light"] = dict(sun["light"], type="Directional", intensity=3.0, color=[1.0, 0.95, 0.88], castShadows=True)

cam = add(cam_t, name="Main Camera", transform=([0, 2.2, 7.5], [-12, 0, 0], [1, 1, 1]))

scene = {
    "activeCameraId": cam["id"],
    "environment": copy.deepcopy(base["environment"]),
    "name": "rt_test",
    "nextId": len(nodes) + 1,
    "nodes": nodes,
}
env = scene["environment"]
env["enableSky"] = True
env["enableShadows"] = True
env["timeOfDay"] = env.get("timeOfDay", 15.0)

json.dump(scene, open(DST, "w", encoding="utf-8"), indent=2)
print("wrote", DST)
