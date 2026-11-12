#!/usr/bin/env python3
"""Merge the CFace `stop` hook into a Cursor hooks.json (stdlib only).

  python merge-hook.py --hooks ~/.cursor/hooks.json --command 'python "/path/notify-oled.py"'

Creates the file if missing, keeps every other hook, and never duplicates:
an existing stop entry that runs notify-oled.py is updated in place.
"""
import argparse
import json
import sys
from pathlib import Path


def merge(data: dict, command: str) -> tuple[dict, str]:
    if not isinstance(data, dict):
        raise ValueError("hooks.json must contain a JSON object")
    data.setdefault("version", 1)
    hooks = data.setdefault("hooks", {})
    stop = hooks.setdefault("stop", [])
    for entry in stop:
        if "notify-oled.py" in str(entry.get("command", "")):
            if entry.get("command") == command:
                return data, "already present"
            entry["command"] = command
            entry.setdefault("timeout", 5)
            return data, "updated"
    stop.append({"command": command, "timeout": 5})
    return data, "added"


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--hooks", required=True)
    p.add_argument("--command", required=True)
    args = p.parse_args()
    path = Path(args.hooks).expanduser()
    data = {}
    if path.is_file() and path.read_text(encoding="utf-8").strip():
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except json.JSONDecodeError as exc:
            print(f"{path} is not valid JSON ({exc}); not touching it.", file=sys.stderr)
            return 1
    data, result = merge(data, args.command)
    if result != "already present":
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    print(f"stop hook {result}: {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
