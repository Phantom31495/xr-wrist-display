#!/bin/bash
# Feature Scaffold launcher — serves the web UI on http://127.0.0.1:8890
cd "$(dirname "$0")"
python3 -c "import flask" 2>/dev/null || pip install --break-system-packages flask -q
exec python3 app.py
