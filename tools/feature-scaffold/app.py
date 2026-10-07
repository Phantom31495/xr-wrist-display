"""Feature Scaffold UI — developer tool for the XR Wrist Display project.

Generates production-ready boilerplate for new features:
  Quest  -> C++ OpenXR scaffold (header + impl + integration notes)
  Phone  -> Kotlin scaffold (class / Activity / Service + manifest notes)

Run:  ./run.sh   (serves on http://127.0.0.1:8890)
"""

import io
import os
import re
import zipfile

from flask import Flask, jsonify, render_template, request, send_file

from generators.quest import generate_quest
from generators.phone import generate_phone

app = Flask(__name__)

PROJECT_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", ".."))

# Safety: copy-to-project may only write under these roots.
ALLOWED_COPY_PREFIXES = (
    os.path.join(PROJECT_ROOT, "quest", "app", "src", "main", "cpp") + os.sep,
    os.path.join(PROJECT_ROOT, "phone", "app", "src", "main", "java",
                 "com", "zachery", "xrwrist", "phone") + os.sep,
    os.path.join(PROJECT_ROOT, "quest") + os.sep + "INTEGRATION_",
    os.path.join(PROJECT_ROOT, "phone") + os.sep + "INTEGRATION_",
)

VALID_TARGETS = {"quest", "phone", "both"}
VALID_TYPES = {"renderer", "input", "network", "panel", "devtool", "settings"}


def _sanitize_name(name: str) -> str:
    name = re.sub(r"[^A-Za-z0-9]", "", (name or "").strip())
    if not name:
        raise ValueError("Feature name is required (letters/digits only).")
    return name[:1].upper() + name[1:]


def generate_all(name: str, target: str, ftype: str, description: str):
    files = []
    if target in ("quest", "both"):
        files.extend(generate_quest(name, ftype, description))
    if target in ("phone", "both"):
        files.extend(generate_phone(name, ftype, description))
    return files


@app.get("/")
def index():
    return render_template("index.html")


@app.post("/api/generate")
def api_generate():
    try:
        data = request.get_json(force=True) or {}
        name = _sanitize_name(data.get("name", ""))
        target = data.get("target", "quest")
        ftype = data.get("ftype", "devtool")
        description = (data.get("description") or "").strip()
        if target not in VALID_TARGETS:
            return jsonify({"error": "Invalid target"}), 400
        if ftype not in VALID_TYPES:
            return jsonify({"error": "Invalid feature type"}), 400
        files = generate_all(name, target, ftype, description)
        return jsonify({"feature": name, "files": files})
    except ValueError as e:
        return jsonify({"error": str(e)}), 400


@app.post("/api/download")
def api_download():
    data = request.get_json(force=True) or {}
    try:
        name = _sanitize_name(data.get("name", ""))
        target = data.get("target", "quest")
        ftype = data.get("ftype", "devtool")
        description = (data.get("description") or "").strip()
        files = generate_all(name, target, ftype, description)
    except ValueError as e:
        return jsonify({"error": str(e)}), 400

    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
        for f in files:
            z.writestr(f["path"], f["content"])
    buf.seek(0)
    return send_file(buf, mimetype="application/zip",
                     as_attachment=True,
                     download_name=f"{name.lower()}_scaffold.zip")


@app.post("/api/copy")
def api_copy():
    """Write generated files into the project tree (allow-listed paths only)."""
    data = request.get_json(force=True) or {}
    try:
        name = _sanitize_name(data.get("name", ""))
        target = data.get("target", "quest")
        ftype = data.get("ftype", "devtool")
        description = (data.get("description") or "").strip()
        files = generate_all(name, target, ftype, description)
    except ValueError as e:
        return jsonify({"error": str(e)}), 400

    written, skipped = [], []
    for f in files:
        dest = os.path.abspath(os.path.join(PROJECT_ROOT, f["path"]))
        if not dest.startswith(ALLOWED_COPY_PREFIXES):
            skipped.append(f["path"])
            continue
        if os.path.exists(dest):
            skipped.append(f["path"] + " (exists — not overwritten)")
            continue
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        with open(dest, "w") as fh:
            fh.write(f["content"])
        written.append(f["path"])
    return jsonify({"written": written, "skipped": skipped})


if __name__ == "__main__":
    app.run(host="127.0.0.1", port=8890)
