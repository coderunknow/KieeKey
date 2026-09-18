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
# owns an ArcadeManager), PLAYS each game through the same /api/input and
# /api/text the browser uses, and keeps the richest frame it saw. Playing (as
# opposed to starting and snapshotting) matters: a freshly started Snake has a
# three-cell snake and no food, a freshly started Tetris an empty well — those
# frames render as near-empty canvases and prove nothing about the UI/UX.
#
# The auto-players read the screen they are being captured from (the typing
# games type the passage the engine just drew, the rhythm player presses the
# lane whose note is closest to the hit line), which keeps the fixtures honest:
# no ground truth is injected from outside the engine.
#
# Fixtures are snapshots of a live run, not golden values: the engine is driven
# by real time, so scores and piece positions differ between captures.
# tests/web_render_test.js therefore asserts structural invariants (command
# count, mapping, text reaching fillText), and the PNGs in
# docs/bench/arcade-130/frames/ are labelled as snapshots.
#
# Usage:
#   /tmp/kt/arcade_serve --port 8765 --web web &
#   python3 tests/capture_web_frames.py [--base http://127.0.0.1:8765]
#   node tests/web_render_test.js
#   node tests/render_web_frames.js
#----------------------------------------------------------------------------
import argparse
import json
import pathlib
import random
import sys
import time
import urllib.request

SLUGS = ["snake", "tetris", "fishing", "typing-race", "wasd-race", "rhythm",
         "no-mistake", "flexing"]

# Virtual-key codes (they mirror the arcade's own vk:: namespace).
VK_LEFT, VK_UP, VK_RIGHT, VK_DOWN, VK_SPACE = 0x25, 0x26, 0x27, 0x28, 0x20
VK_A, VK_D, VK_F, VK_J, VK_K, VK_S, VK_W = 0x41, 0x44, 0x46, 0x4A, 0x4B, 0x53, 0x57
LANE_VK = {"D": VK_D, "F": VK_F, "J": VK_J, "K": VK_K}

FLEX_PASSAGE = ("Flexing Mode: văn bản này được chuẩn bị trước và engine tự gõ nó "
                "ra cho bạn — không cần gõ một phím nào cho từng ký tự.")

# Command kinds in the wire format (mirrors web/arcade.js KIND):
RECT, CIRCLE, LINE, POLY, TEXT = 0, 1, 2, 3, 4


class Bridge:
    def __init__(self, base: str):
        self.base = base

    def post(self, path, body):
        request = urllib.request.Request(
            self.base + path, data=json.dumps(body).encode(),
            headers={"Content-Type": "application/json"}, method="POST")
        with urllib.request.urlopen(request, timeout=5) as response:
            return json.loads(response.read())

    def get(self, path):
        with urllib.request.urlopen(self.base + path, timeout=5) as response:
            return json.loads(response.read())

    def state(self):
        return self.get("/api/state")

    def key(self, vk, ch=""):
        body = {"vk": vk, "down": True}
        if ch:
            body["ch"] = ch
        self.post("/api/input", body)
        self.post("/api/input", {"vk": vk, "down": False})

    def text(self, text):
        self.post("/api/text", {"text": text})

    def start(self, slug):
        self.post("/api/start", {"slug": slug})

    def restart(self):
        self.post("/api/restart", {})


#---------------------------------------------------------------------------
# Frame reading helpers — the auto-players only use what the engine drew.
#---------------------------------------------------------------------------
def text_shapes(frame):
    """(x, y, size, text) of every text command in the frame."""
    return [(c[1], c[2], c[3], c[8]) for c in frame.get("cmds", []) if c[0] == TEXT]


def typing_target(frame):
    """The passage the game is asking for: the big text drawn below the HUD."""
    height = frame.get("h", 620)
    shapes = [s for s in text_shapes(frame) if s[2] >= 26 and s[1] > height * 0.5]
    shapes.sort(key=lambda s: (s[1], s[0]))
    return "".join(s[3] for s in shapes)


def rhythm_lanes(frame):
    """{lane letter: (centre x, lane colour)} from the lane labels."""
    return {s[3]: (s[0], None) for s in text_shapes(frame) if s[3] in LANE_VK}


def rhythm_next_lane(frame):
    """The lane whose falling note is closest to the hit line (largest y)."""
    lanes = {letter: x for letter, (x, _) in rhythm_lanes(frame).items()}
    if not lanes:
        return None
    best, best_y = None, -1.0
    for c in frame.get("cmds", []):
        if c[0] != RECT:
            continue
        _, x, y, w, h, _radius, fill, _stroke, _sw = c[:9]
        if abs(w - 78.0) > 1.0 or abs(h - 32.0) > 1.0:      # note size
            continue
        for letter, cx in lanes.items():
            if abs((x + w * 0.5) - cx) < 6.0 and y > 60 and y < 552 and y > best_y:
                best, best_y = letter, y
    return best


#---------------------------------------------------------------------------
# Per-game auto-players: `act()` is called once per sample interval and may use
# the frame that was just captured.
#---------------------------------------------------------------------------
class Player:
    def act(self, bridge, frame, step):
        raise NotImplementedError

    def on_start(self, bridge):
        pass


class SnakePlayer(Player):
    """Travel in long straight runs so the snake actually crosses the board."""

    LEGS = [(VK_RIGHT, 14), (VK_DOWN, 10), (VK_LEFT, 14), (VK_UP, 10)]

    def __init__(self):
        self.leg = 0
        self.steps_in_leg = 0

    def act(self, bridge, frame, step):
        if frame.get("stats", {}).get("lives") == 0:
            bridge.restart()
            return
        key, length = self.LEGS[self.leg]
        bridge.key(key)
        self.steps_in_leg += 1
        if self.steps_in_leg >= length:
            self.steps_in_leg = 0
            self.leg = (self.leg + 1) % len(self.LEGS)


class TetrisPlayer(Player):
    """Stack pieces fast: move, rotate, hard-drop."""

    MOVES = [VK_LEFT, VK_LEFT, VK_UP, VK_SPACE, VK_RIGHT, VK_UP, VK_DOWN, VK_SPACE,
             VK_RIGHT, VK_UP, VK_SPACE, VK_DOWN]

    def act(self, bridge, frame, step):
        bridge.key(self.MOVES[step % len(self.MOVES)])


class TypingPlayer(Player):
    """Type exactly what the engine drew, in small chunks."""

    CHUNK = 4

    def __init__(self):
        self.queue = []

    def act(self, bridge, frame, step):
        if frame.get("gameOver") or not self.queue:
            target = typing_target(frame)
            if not target:
                bridge.restart()
                return
            self.queue = list(target)
        chunk = "".join(self.queue[:self.CHUNK])
        del self.queue[:self.CHUNK]
        if chunk:
            bridge.text(chunk)


class WasdPlayer(TypingPlayer):
    """Same as TypingPlayer, plus the dodging half of the game."""

    def act(self, bridge, frame, step):
        super().act(bridge, frame, step)
        if step % 4 == 0:
            bridge.key(VK_A if step % 8 == 0 else VK_D)


class RhythmPlayer(Player):
    """Hit the note that is closest to the hit line."""

    def act(self, bridge, frame, step):
        letter = rhythm_next_lane(frame)
        if letter is None:
            bridge.key(LANE_VK["D"], "d")
            return
        bridge.key(LANE_VK[letter], letter.lower())


class FlexingPlayer(Player):
    """Flexing turns any keypress into engine-produced text."""

    KEYS = [VK_A, VK_S, VK_D, VK_W]

    def on_start(self, bridge):
        # The prepared passage has to be loaded first, exactly like the page.
        bridge.post("/api/preload", {"text": FLEX_PASSAGE, "granularity": 0})

    def act(self, bridge, frame, step):
        bridge.key(self.KEYS[step % len(self.KEYS)])


PLAYERS = {
    "snake": SnakePlayer,
    "tetris": TetrisPlayer,
    "fishing": TypingPlayer,
    "typing-race": TypingPlayer,
    "wasd-race": WasdPlayer,
    "rhythm": RhythmPlayer,
    "no-mistake": TypingPlayer,
    "flexing": FlexingPlayer,
}


def richness(frame) -> int:
    """How much of the game is on screen (used to pick the best sample)."""
    return len(frame.get("cmds", []))


def capture(bridge, slug, seconds, interval, seed):
    """Play `slug` for `seconds` and return the richest frame seen."""
    player = PLAYERS[slug]()
    bridge.start(slug)
    player.on_start(bridge)

    best, best_meta, best_rank = None, {}, (-1, -1)
    deadline = time.time() + seconds
    step = 0
    while time.time() < deadline:
        state = bridge.state()
        frame = state.get("frame") or {}
        try:
            player.act(bridge, frame, step)
        except Exception as error:            # noqa: BLE001 - keep capturing
            print(f"    (player error: {error})", file=sys.stderr)
        state = bridge.state()
        frame = state.get("frame") or {}
        # Rank by "how much is on screen", then by how long the game has been
        # played: several games draw the same number of commands from the first
        # frame on (their progress is in the shape data), and for those the
        # later frame is the interesting one.
        rank = (richness(frame), int(state.get("elapsed") or 0))
        if best is None or rank > best_rank:
            best, best_rank = frame, rank
            best_meta = {
                "score": (frame.get("stats") or {}).get("score", 0),
                "elapsed": state.get("elapsed"),
                "gameOver": state.get("gameOver"),
            }
        time.sleep(interval)
        step += 1
    return best, best_meta


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", default="http://127.0.0.1:8765")
    parser.add_argument("--out", default="tests/data/web_frames.json")
    parser.add_argument("--seconds", type=float, default=4.0,
                        help="auto-play time per game before sampling stops")
    parser.add_argument("--interval", type=float, default=0.08,
                        help="seconds between samples")
    parser.add_argument("--seed", type=int, default=20260918)
    args = parser.parse_args()

    bridge = Bridge(args.base)
    try:
        bridge.get("/api/ping")
    except Exception as error:                     # noqa: BLE001 - report and exit
        print(f"cannot reach the bridge at {args.base}: {error}", file=sys.stderr)
        print("start it first: /tmp/kt/arcade_serve --port 8765 --web web", file=sys.stderr)
        return 2

    random.seed(args.seed)

    # Mistakes must not end a typing run mid-capture: this capture is about what
    # the screen shows, and the fail mode is a runtime knob (the arcade's own
    # default stays Hardcore).
    bridge.post("/api/config", {"noMistakeFailMode": 1, "rhythmFailMode": 1})

    payload = {
        "note": ("Captured from the real C++ engine through the HTTP bridge "
                 "(tools/arcade_serve): each game is auto-played for a few "
                 "seconds through /api/input + /api/text — the typing players "
                 "type the passage the engine just drew, the rhythm player hits "
                 "the lane nearest the hit line — and the richest frame is kept. "
                 "Replay material for tests/web_render_test.js and "
                 "tests/render_web_frames.js."),
        "captured_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "capture": {
            "driver": "tests/capture_web_frames.py",
            "seconds_per_game": args.seconds,
            "sample_interval": args.interval,
            "seed": args.seed,
            "selection": "frame with the most draw commands during the window",
        },
        "games": {},
    }
    for slug in SLUGS:
        frame, meta = capture(bridge, slug, args.seconds, args.interval, args.seed)
        payload["games"][slug] = {
            "slug": slug,
            "frame": frame,
            "stats": frame.get("stats", {}),
            "cmds": len(frame.get("cmds", [])),
            "played": meta,
        }
        print(f"  {slug:12s} {len(frame.get('cmds', [])):3d} commands  "
              f"score={meta.get('score')}  {frame.get('title')}")
    bridge.post("/api/stop", {})

    path = pathlib.Path(args.out)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"wrote {path} ({path.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
