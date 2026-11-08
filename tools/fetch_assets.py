"""Downloads the CC0 models and textures the Headland showcase world uses, from Poly Haven.

    python tools/fetch_assets.py                 # everything in tools/assets_manifest.json
    python tools/fetch_assets.py --dry-run       # list what would be downloaded, and the size
    python tools/fetch_assets.py --only pine_tree_01 coast_sand_01

Files go to assets/external/polyhaven/ (git-ignored):
    models/<id>/<id>_<res>.gltf (+ .bin, textures/)
    textures/<id>/<id>_<map>_<res>.jpg
    CREDITS.md

Downloads resume: files already present with the right MD5 are skipped.

Leaf cards: Poly Haven's glTF exports reference plain JPG colour maps for foliage and ship the
cut-out mask as a separate "<part>_alpha" texture. The engine (like any glTF viewer) needs the
mask in the colour map's alpha channel with alphaMode MASK, so for every material that has an
alpha texture this script writes an RGBA PNG (colour + mask) and points the material at it.
That step needs Pillow (pip install pillow); without it the leaves stay opaque.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
import sys
import threading
import time
import urllib.error
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
API = "https://api.polyhaven.com"
USER_AGENT = "TesseraAssetFetcher/1.0 (Tessera engine asset tool)"
print_lock = threading.Lock()


def log(*args):
    with print_lock:
        print(*args, flush=True)


def get_json(url, retries=4):
    for attempt in range(retries):
        try:
            req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
            with urllib.request.urlopen(req, timeout=60) as r:
                return json.load(r)
        except (urllib.error.URLError, TimeoutError) as e:
            if attempt == retries - 1:
                raise
            time.sleep(1.5 * (attempt + 1))


def md5_of(path):
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def download(url, dest, md5=None, retries=4):
    """Downloads url to dest (atomically). Skips when dest exists with the right MD5."""
    if os.path.exists(dest) and (md5 is None or md5_of(dest) == md5):
        return False
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    tmp = dest + ".part"
    for attempt in range(retries):
        try:
            req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
            with urllib.request.urlopen(req, timeout=120) as r, open(tmp, "wb") as f:
                while True:
                    chunk = r.read(1 << 20)
                    if not chunk:
                        break
                    f.write(chunk)
            if md5 and md5_of(tmp) != md5:
                raise IOError("MD5 mismatch for " + url)
            os.replace(tmp, dest)
            return True
        except (urllib.error.URLError, TimeoutError, IOError) as e:
            if attempt == retries - 1:
                raise RuntimeError(f"failed to download {url}: {e}")
            time.sleep(2.0 * (attempt + 1))


# --------------------------------------------------------------------------------------------
# Planning: which files each asset needs
# --------------------------------------------------------------------------------------------

def plan_model(asset_id, res, out_dir):
    files = get_json(f"{API}/files/{asset_id}")
    gltf = files.get("gltf", {})
    if res not in gltf:
        res = sorted(gltf.keys(), key=lambda r: abs(int(r[:-1]) - int(res[:-1])))[0]
    entry = gltf[res]["gltf"]
    base = os.path.join(out_dir, "models", asset_id)
    jobs = [(entry["url"], os.path.join(base, os.path.basename(entry["url"])), entry.get("md5"), entry.get("size", 0))]
    for rel, inc in entry.get("include", {}).items():
        jobs.append((inc["url"], os.path.join(base, rel), inc.get("md5"), inc.get("size", 0)))
    # Alpha masks for foliage parts (e.g. "twig_alpha") at the same resolution
    alphas = {}
    for key, val in files.items():
        if key.endswith("_alpha") and isinstance(val, dict) and res in val:
            fmt = "png" if "png" in val[res] else "jpg"
            a = val[res][fmt]
            dest = os.path.join(base, "textures", os.path.basename(a["url"]))
            jobs.append((a["url"], dest, a.get("md5"), a.get("size", 0)))
            alphas[key[: -len("_alpha")]] = dest
    gltf_path = jobs[0][1]
    return {"id": asset_id, "kind": "model", "res": res, "jobs": jobs, "gltf": gltf_path, "alphas": alphas}


def plan_texture(asset_id, res, maps, out_dir):
    files = get_json(f"{API}/files/{asset_id}")
    base = os.path.join(out_dir, "textures", asset_id)
    jobs = []
    for m in maps:
        if m not in files:
            continue
        r = res if res in files[m] else sorted(files[m].keys(), key=lambda x: abs(int(x[:-1]) - int(res[:-1])))[0]
        fmt = "jpg" if "jpg" in files[m][r] else "png"
        f = files[m][r][fmt]
        jobs.append((f["url"], os.path.join(base, os.path.basename(f["url"])), f.get("md5"), f.get("size", 0)))
    return {"id": asset_id, "kind": "texture", "res": res, "jobs": jobs}


# --------------------------------------------------------------------------------------------
# Leaf cards: colour + separate alpha -> RGBA PNG, material set to alphaMode MASK
# --------------------------------------------------------------------------------------------

def apply_alpha_masks(plan):
    if not plan["alphas"]:
        return 0
    try:
        from PIL import Image
    except ImportError:
        log(f"  ! {plan['id']}: Pillow is not installed, foliage cut-outs skipped (pip install pillow)")
        return 0
    gltf_path = plan["gltf"]
    with open(gltf_path, encoding="utf-8") as f:
        doc = json.load(f)
    gltf_dir = os.path.dirname(gltf_path)
    changed = 0
    images = doc.get("images", [])
    textures = doc.get("textures", [])
    for mat in doc.get("materials", []):
        pbr = mat.get("pbrMetallicRoughness", {})
        bct = pbr.get("baseColorTexture")
        if not bct:
            continue
        img_index = textures[bct["index"]]["source"]
        image = images[img_index]
        uri = image.get("uri", "")
        if uri.endswith(".png") and "_diffalpha_" in uri:
            continue  # done on an earlier run
        # "<id>_<part>_diff_<res>.jpg" -> part
        name = os.path.basename(uri)
        part = None
        for p in plan["alphas"]:
            if f"_{p}_diff_" in name:
                part = p
                break
        if part is None:
            continue
        colour_path = os.path.join(gltf_dir, uri)
        alpha_path = plan["alphas"][part]
        out_name = name.replace("_diff_", "_diffalpha_").rsplit(".", 1)[0] + ".png"
        out_path = os.path.join(os.path.dirname(colour_path), out_name)
        colour = Image.open(colour_path).convert("RGB")
        alpha = Image.open(alpha_path).convert("L")
        if alpha.size != colour.size:
            alpha = alpha.resize(colour.size, Image.LANCZOS)
        rgba = colour.copy()
        rgba.putalpha(alpha)
        rgba.save(out_path, optimize=True)
        image["uri"] = os.path.relpath(out_path, gltf_dir).replace("\\", "/")
        image["mimeType"] = "image/png"
        mat["alphaMode"] = "MASK"
        mat["alphaCutoff"] = 0.5
        changed += 1
    if changed:
        with open(gltf_path, "w", encoding="utf-8") as f:
            json.dump(doc, f, indent=1)
    return changed


# --------------------------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--manifest", default=os.path.join(ROOT, "tools", "assets_manifest.json"))
    ap.add_argument("--out", default=os.path.join(ROOT, "assets", "external", "polyhaven"))
    ap.add_argument("--dry-run", action="store_true", help="list the downloads and their total size")
    ap.add_argument("--only", nargs="*", help="only these asset ids")
    ap.add_argument("--jobs", type=int, default=6, help="parallel downloads")
    args = ap.parse_args()

    manifest = json.load(open(args.manifest, encoding="utf-8"))
    model_ids = [m for group in manifest["models"].values() for m in group]
    texture_ids = list(manifest["textures"].values())
    if args.only:
        model_ids = [m for m in model_ids if m in args.only]
        texture_ids = [t for t in texture_ids if t in args.only]

    log(f"Planning {len(model_ids)} models and {len(texture_ids)} textures...")
    plans = []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        futures = [pool.submit(plan_model, m, manifest["modelResolution"], args.out) for m in model_ids]
        futures += [pool.submit(plan_texture, t, manifest["textureResolution"], manifest["textureMaps"], args.out) for t in texture_ids]
        for fut in futures:
            try:
                plans.append(fut.result())
            except Exception as e:
                log(f"  ! planning failed: {e}")

    jobs = [j for p in plans for j in p["jobs"]]
    total = sum(j[3] for j in jobs)
    log(f"{len(jobs)} files, {total / 1e6:.0f} MB")
    if args.dry_run:
        for p in plans:
            size = sum(j[3] for j in p["jobs"])
            log(f"  {p['kind']:7} {p['id']:28} {len(p['jobs']):3} files {size / 1e6:7.1f} MB"
                + (f"  (cut-out parts: {', '.join(p['alphas'])})" if p.get("alphas") else ""))
        return 0

    done = [0, 0]  # files, bytes

    def run(job):
        url, dest, md5, size = job
        fetched = download(url, dest, md5)
        with print_lock:
            done[0] += 1
            done[1] += size
            if fetched and done[0] % 10 == 0:
                print(f"  {done[0]}/{len(jobs)} files, {done[1] / 1e6:.0f}/{total / 1e6:.0f} MB", flush=True)

    errors = 0
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        for fut in [pool.submit(run, j) for j in jobs]:
            try:
                fut.result()
            except Exception as e:
                errors += 1
                log(f"  ! {e}")

    cutouts = 0
    for p in plans:
        if p["kind"] == "model":
            try:
                cutouts += apply_alpha_masks(p)
            except Exception as e:
                errors += 1
                log(f"  ! {p['id']}: alpha masks failed: {e}")
    log(f"Foliage materials given alpha cut-outs: {cutouts}")

    # Credits (CC0 needs none, but the authors deserve them)
    lines = ["# Third-party assets", "",
             "Downloaded from [Poly Haven](https://polyhaven.com) by `tools/fetch_assets.py`.",
             "All assets are **CC0** (public domain). Thank you to their authors.", "",
             "| Asset | Type | Authors |", "| :--- | :--- | :--- |"]
    for p in plans:
        try:
            info = get_json(f"{API}/info/{p['id']}")
            authors = ", ".join(info.get("authors", {}).keys())
        except Exception:
            authors = "?"
        lines.append(f"| [{p['id']}](https://polyhaven.com/a/{p['id']}) | {p['kind']} | {authors} |")
    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, "CREDITS.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")

    log("Done." if errors == 0 else f"Done with {errors} error(s).")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
