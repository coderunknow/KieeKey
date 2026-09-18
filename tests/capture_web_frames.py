#!/usr/bin/env python3
#============================================================================
# KieeKey - A modified version based on OpenKey
#
# Modified work:
#   KieeKey - refactored and completed logic
#   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
#   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
#
# File: tests/capture_web_frames.py
# SPDX-License-Identifier: GPL-3.0-or-later
#============================================================================
#----------------------------------------------------------------------------
# tests/capture_web_frames.py — refresh tests/data/web_frames.json.
#
# The web renderer test replays real frames, so the fixtures must come from the
# real engine. This script talks to a running bridge (tools/arcade_serve, which
# owns an ArcadeManager), starts every game, lets it simulate for a moment and
# dumps the frame JSON the browser would have received.
#
# Usage:
#   /tmp/kt/arcade_serve --port 8765 --web web &
#   python3 tests/capture_web_frames.py [--base http://127.0.0.1:8765]
#   node tests/web_render_test.js
#----------------------------------------------------------------------------
import argparse
import json
import pathlib
import sys
import time
import urllib.request

SLUGS = ["snake", "tetris", "fishing", "typing-race", "wasd-race", "rhythm",
         "no-mistake", "flexing"]


def post(base, path, body):
    request = urllib.request.Request(
        base + path, data=json.dumps(body).encode(),
        headers={"Content-Type": "application/json"}, method="POST")
    with urllib.request.urlopen(request, timeout=5) as response:
        return json.loads(response.read())


def get(base, path):
    with urllib.request.urlopen(base + path, timeout=5) as response:
        return json.loads(response.read())


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", default="http://127.0.0.1:8765")
    parser.add_argument("--out", default="tests/data/web_frames.json")
    args = parser.parse_args()

    try:
        get(args.base, "/api/ping")
    except Exception as error:                     # noqa: BLE001 - report and exit
        print(f"cannot reach the bridge at {args.base}: {error}", file=sys.stderr)
        print("start it first: /tmp/kt/arcade_serve --port 8765 --web web", file=sys.stderr)
        return 2

    payload = {
        "note": ("Captured from the real C++ engine through the HTTP bridge "
                 "(tools/arcade_serve) — replay fixtures for "
                 "tests/web_render_test.js."),
        "captured_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "games": {},
    }
    for slug in SLUGS:
        post(args.base, "/api/start", {"slug": slug})
        # One keystroke + a few simulated frames, so the fixture is not the
        # pristine opening state of every game.
        post(args.base, "/api/input", {"vk": 65, "down": True})
        post(args.base, "/api/input", {"vk": 65, "down": False})
        time.sleep(0.35)
        state = get(args.base, "/api/state")
        payload["games"][slug] = {
            "slug": slug,
            "frame": state["frame"],
            "stats": state["frame"]["stats"],
            "cmds": len(state["frame"]["cmds"]),
        }
        print(f"  {slug:12s} {len(state['frame']['cmds']):3d} commands  "
              f"{state['frame']['title']}")
    post(args.base, "/api/stop", {})

    path = pathlib.Path(args.out)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"wrote {path} ({path.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
