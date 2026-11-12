#!/usr/bin/env python3
"""Tell a CFace ESP32 that a Cursor agent turn finished.

Cursor runs this from the `stop` hook (see the project README). The hook
writes a JSON payload to stdin. This process posts a short line to the board
and always prints `{}` on stdout, so Cursor does not treat it as a follow-up
and will not start another turn.

Stdlib only. No pip packages.

  python3 notify-oled.py --text "New reply"     # manual test
  python3 notify-oled.py --dry-run              # show the POST, send nothing
  python3 notify-oled.py --self-test
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

DEFAULT_TEXT = "New reply"
MAX_CHARS = 20
PLACEHOLDER_HOSTS = {"esp32_ip_here", "your_esp32_ip", "your.esp32.ip"}


def shorten(text: str, fallback: str = DEFAULT_TEXT) -> str:
    cleaned = "".join(ch for ch in text if 32 <= ord(ch) <= 126).strip()
    if not cleaned:
        cleaned = fallback
    if len(cleaned) <= MAX_CHARS:
        return cleaned
    clipped = cleaned[: MAX_CHARS - 3].rstrip()
    return (clipped or cleaned[: MAX_CHARS - 3]) + "..."


def choose_text(payload: dict, cli_text: str | None, completed_text: str) -> str:
    """Map a hook payload to the single bottom line the OLED can show.

    The assistant's full reply is never sent. A 128x64 panel has room for
    about 20 characters, and the reply itself is not what the face should say.
    """
    if cli_text is not None:
        return shorten(cli_text, completed_text or DEFAULT_TEXT)

    event = str(payload.get("hook_event_name") or "")
    if event == "stop":
        status = str(payload.get("status") or "completed")
        if status == "error":
            return shorten("Hit a snag")
        if status == "aborted":
            return shorten("Stopped")
        return shorten(completed_text or DEFAULT_TEXT)

    return shorten(completed_text or DEFAULT_TEXT)


def parse_env_file(path: Path) -> dict[str, str]:
    found: dict[str, str] = {}
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError:
        return found
    for raw in lines:
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("export "):
            line = line[len("export ") :].strip()
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        key = key.strip()
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in {'"', "'"}:
            value = value[1:-1]
        if key:
            found[key] = value
    return found


def load_settings(script_path: Path) -> tuple[str, str]:
    """URL, then the optional completed-turn caption.

    Precedence for each key: environment, config.env beside this script,
    then ~/.cursor/oled-face.env.
    """
    files = [
        script_path.with_name("config.env"),
        Path.home() / ".cursor" / "oled-face.env",
    ]
    from_files: dict[str, str] = {}
    for path in files:
        if path.is_file():
            from_files = parse_env_file(path)
            break

    url = os.environ.get("ESP32_FACE_URL", "").strip() or from_files.get("ESP32_FACE_URL", "").strip()
    completed = os.environ.get("FACE_TEXT", "").strip() or from_files.get("FACE_TEXT", "").strip()
    return url, completed or DEFAULT_TEXT


def read_payload() -> dict:
    if sys.stdin.isatty():
        return {}
    raw = sys.stdin.read()
    if not raw.strip():
        return {}
    try:
        data = json.loads(raw)
    except json.JSONDecodeError:
        return {}
    return data if isinstance(data, dict) else {}


def url_is_usable(url: str) -> bool:
    if not url:
        return False
    try:
        parts = urllib.parse.urlparse(url)
    except ValueError:
        return False
    host = (parts.hostname or "").lower()
    if parts.scheme not in {"http", "https"} or not host:
        return False
    return host not in PLACEHOLDER_HOSTS


def clear_url(url: str) -> str:
    parts = urllib.parse.urlparse(url)
    return urllib.parse.urlunparse(parts._replace(path="/clear"))


def post_event(url: str, text: str, timeout: float, body: bytes | None = None) -> tuple[bool, str]:
    if body is None:
        body = json.dumps({"event": "prompt_complete", "text": text}).encode("utf-8")
    request = urllib.request.Request(
        url,
        data=body,
        method="POST",
        headers={"Content-Type": "application/json", "Accept": "application/json"},
    )
    # An ambient HTTP proxy would swallow a LAN address. Talk to the board directly.
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    try:
        with opener.open(request, timeout=timeout) as response:
            detail = response.read(200).decode("utf-8", "replace").strip()
            return True, detail or "ok"
    except urllib.error.HTTPError as exc:
        detail = exc.read(200).decode("utf-8", "replace").strip()
        return False, "HTTP %s %s" % (exc.code, detail)
    except Exception as exc:  # noqa: BLE001 — hook must report and keep going
        return False, str(exc)


def emit_hook_result() -> None:
    # Empty object: no followup_message, so a stop hook cannot loop the agent.
    sys.stdout.write("{}\n")
    sys.stdout.flush()


def run_self_test() -> int:
    cases = [
        ({"hook_event_name": "stop", "status": "completed"}, None, "New reply", "New reply"),
        ({"hook_event_name": "stop", "status": "error"}, None, "New reply", "Hit a snag"),
        ({"hook_event_name": "stop", "status": "aborted"}, None, "New reply", "Stopped"),
        (
            {"hook_event_name": "afterAgentResponse", "text": "A very long assistant reply"},
            None,
            "New reply",
            "New reply",
        ),
        ({"hook_event_name": "stop", "status": "completed"}, None, "All done", "All done"),
        ({}, "Hello there", "New reply", "Hello there"),
        ({"hook_event_name": "stop", "status": "completed"}, None, "New reply", "New reply"),
    ]
    failed = False
    for payload, cli, completed, expected in cases:
        got = choose_text(payload, cli, completed)
        if got != expected:
            print("FAIL %r -> %r (expected %r)" % (payload, got, expected), file=sys.stderr)
            failed = True
    long_line = shorten("abcdefghijklmnopqrstuvwxyz")
    if long_line != "abcdefghijklmnopq...":
        print("FAIL shorten -> %r" % long_line, file=sys.stderr)
        failed = True
    sample = parse_env_file(Path(os.devnull))
    if sample:
        print("FAIL empty env file parsed as %r" % sample, file=sys.stderr)
        failed = True
    if failed:
        print("self-test failed", file=sys.stderr)
        return 1
    print("self-test ok")
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "mode", nargs="?", choices=["notify", "clear"], default="notify",
        help="clear resets the unread count (POST /clear); the board button does the same",
    )
    parser.add_argument("--text", help="bottom-line text for a manual test")
    parser.add_argument("--dry-run", action="store_true", help="print the POST and do not send it")
    parser.add_argument("--self-test", action="store_true", help="check caption selection and exit")
    parser.add_argument("--verbose", action="store_true", help="log the result on stderr")
    parser.add_argument("--timeout", type=float, default=2.0, help="HTTP timeout in seconds")
    args = parser.parse_args(argv)

    if args.self_test:
        # Still answer with {} when a hook accidentally passes the flag? No —
        # self-test is a manual command and reports in plain text.
        return run_self_test()

    hook_mode = not sys.stdin.isatty()
    payload = read_payload()
    url, completed_text = load_settings(Path(__file__).resolve())
    text = choose_text(payload, args.text, completed_text)
    body = json.dumps({"event": "prompt_complete", "text": text})
    if args.mode == "clear":
        url = clear_url(url) if url_is_usable(url) else url
        body = "{}"

    def note(message: str) -> None:
        if args.verbose or args.dry_run or not hook_mode:
            print(message, file=sys.stderr)

    if args.dry_run:
        note("POST %s %s" % (url or "(ESP32_FACE_URL not set)", body))
        emit_hook_result()
        return 0

    if not url_is_usable(url):
        note("Set ESP32_FACE_URL in ~/.cursor/hooks/config.env to the IP on the OLED.")
        emit_hook_result()
        return 0 if hook_mode else 1

    if args.mode == "clear":
        ok, detail = post_event(url, text, args.timeout, b"{}")
        note("CFace unread cleared" if ok else "CFace clear failed (%s)" % detail)
        emit_hook_result()
        return 0 if ok or hook_mode else 1

    ok, detail = post_event(url, text, args.timeout)
    if ok:
        note("CFace notified: %s" % text)
    else:
        note("CFace notify failed (%s). The face stays on its idle animation." % detail)
    emit_hook_result()
    if ok or hook_mode:
        return 0
    return 1


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except BrokenPipeError:
        # Cursor closed the pipe. Do not turn that into a hook failure.
        try:
            sys.stdout.close()
        except Exception:
            pass
        raise SystemExit(0)
