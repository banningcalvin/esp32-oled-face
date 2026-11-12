## Quick setup for the companion (any laptop)

The laptop must be on the same network as the board to reach its IP. Needs `git` and Python 3.8+. Enter the IP shown on the OLED at boot when prompted (use `ip:port` only if you changed the port from 80). The script writes `companion/config.env`, sends a test notification, and merges the Cursor `stop` hook into `~/.cursor/hooks.json` (created if missing, other hooks kept, never duplicated). Restart Cursor afterwards.

PowerShell (Windows):

```powershell
git clone https://github.com/banningcalvin/esp32-oled-face $HOME\cface
& $HOME\cface\companion\setup.ps1
```

bash (macOS/Linux):

```bash
git clone https://github.com/banningcalvin/esp32-oled-face ~/cface
~/cface/companion/setup.sh
```

Re-run the same script any time to change the IP; add `-DryRun` (PowerShell) or `--dry-run` (bash) to preview. To pass the IP non-interactively: `setup.ps1 -Board 192.168.x.x` or `setup.sh 192.168.x.x`.

# CFace

An always-on face for a 0.96 inch SSD1306 OLED on an ESP32. Selectable faces: `idle` (the original), `smoker` (hi-tech, cigarette), and `yesman` (a screen-headed grin). The board boots on `BOOT_FACE` asleep (unread is 0) and wakes on the first notification. When a Cursor agent turn finishes, a small companion on your computer posts to the board and the active face plays its alert.

`idle` widens its eyes. `smoker` inhales, exhales, and shows a bubble icon. `yesman` perks up and shows a bubble icon. The posted text and JSON scroll in the top overlay. All three are described in [What the face does](#what-the-face-does).

## Wiring

Match the **labels on the OLED**, not a fixed connector order. Four-pin modules shuffle GND, VCC, SCL, and SDA.

| OLED pin | ESP32 |
| --- | --- |
| VCC | **3V3** (not the 5V pin) |
| GND | GND |
| SDA | GPIO **21** |
| SCL | GPIO **22** |

Those are the defaults in `include/config.h`. Use the GPIO numbers printed on an ESP32 dev board (often `21` / `G21`), not the `D21` labels from an ESP8266 NodeMCU.

LED on GPIO 25 blinks 500ms on/off for 3 seconds on a notify, then stays solid ON while unread > 0. It turns OFF when unread is cleared (button or `POST /clear`). A new notification while unread restarts the 3 second blink, then back to solid.

A 3-pin potentiometer picks the face. Wire one outer leg to 3V3, the other outer leg to GND, and the wiper (middle pin) to GPIO **33** (`POT_PIN` in `include/config.h`). GPIO 33 is on ADC1, which keeps working with Wi-Fi on.

| Potentiometer pin | ESP32 |
| --- | --- |
| Outer leg | 3V3 |
| Outer leg | GND |
| Wiper (middle) | GPIO **33** |

A momentary push button clears the unread count. Wire one leg to GPIO **32** (`BUTTON_PIN` in `include/config.h`) and the other leg to GND. The firmware uses `INPUT_PULLUP` and debounces, so no resistor is needed.

| Button pin | ESP32 |
| --- | --- |
| Leg 1 | GPIO **32** |
| Leg 2 | GND |

Turn the knob to pick a face: its range is split evenly across the faces, so one end is idle, the middle is smoker, and the other end is yesman.

Assumed panel: SSD1306, 128×64, I2C. The firmware probes `0x3C`, then `0x3D`. This build does not drive SH1106 modules.

## Screen layout

Faces render across the full 128×64 panel. When idle, only the face is shown: no text and no unread line.

- **Overlay (y 0..15, transient):** while an alert is showing, the top 16 rows are cleared and the alert text (or `got a new reply {json}`, or a brief Wi-Fi/IP status) is drawn there; text longer than 21 characters scrolls. The face stays drawn below. When the alert ends the strip goes back to the face.
- **Bubble icon:** on smoker and yesman a small speech-bubble icon with animated dots appears under the strip while a reply message is scrolling. The smoker still inhales, exhales, and puffs at full size.

The scrolled JSON is flattened to one line and capped at about 200 characters. The board still accepts bodies up to 4KB.

## Unread count

The board counts each accepted `POST /notify`. The count is kept internally and reported as `unread` by `GET /health`. It is cleared (to 0) by:

- a short press of the button on GPIO 32, or
- `POST /clear` (manual use; nothing clears it automatically, not even submitting a prompt).

```bash
curl -s -X POST http://192.168.1.42/clear      # {"ok":true,"unread":0}
python3 companion/notify-oled.py clear         # same, using config.env
```

The count lives in RAM and resets on reboot.

## Libraries

PlatformIO fetches these from `platformio.ini`. Nothing to install by hand in the Arduino Library Manager.

- Adafruit SSD1306
- Adafruit GFX Library (and Adafruit BusIO, pulled in with it)
- ArduinoJson
- ESP32 Arduino core pieces already in the platform: Wi-Fi, WebServer, ESPmDNS

The computer-side script uses Python 3's standard library only.

## Configure the board

Edit **`include/config.h`** only, then flash again.

- `OLED_SDA_PIN`, `OLED_SCL_PIN` — I2C pins
- `OLED_I2C_ADDR`, `OLED_I2C_ADDR_FALLBACK` — usually `0x3C` and `0x3D`
- `WIFI_SSID`, `WIFI_PASSWORD` — replace both placeholders. A password that contains `"` or `\` must be escaped
- `HTTP_PORT` — default `80`
- `ALERT_HOLD_MS` — how long the alert expression holds (default 6.5 s)
- `DEFAULT_ALERT_TEXT` — caption used when the POST omits `text` on the idle face
- `BOOT_FACE` — face at power-on: `idle`, `smoker`, or `yesman`. A notify can switch it later with `"face"`. The default in `config.h` is `idle`.

Until `WIFI_SSID` is changed, the board skips Wi-Fi and still animates. The top overlay says `Set WiFi in config.h` for a few seconds.

## Flash

Install [PlatformIO](https://platformio.org/) (`pip install platformio`, or the PlatformIO IDE extension), then from this directory:

```bash
pio run                 # compile
pio run -t upload       # flash over USB
pio device monitor -b 115200
```

If upload fails on a picky USB chip, set `upload_speed = 115200` in `platformio.ini` and upload again.

On boot the face starts moving immediately. After Wi-Fi joins, the top overlay shows `IP a.b.c.d` for about six seconds, then clears. The same URL is printed on the serial monitor every 30 seconds:

```text
CFace listening  http://192.168.1.42:80/notify
```

`http://cface.local/notify` works on some LANs. If it does not resolve, use the IP from the panel. The computer and the ESP32 have to be on the same network, and the access point must allow clients to talk to each other (turn off AP / client isolation).

## How a finished turn reaches the screen

Two pieces, both required:

1. The ESP32 joins Wi-Fi and listens for `POST /notify` with a small JSON body.
2. Cursor's **`stop`** hook on the computer where Cursor runs executes `companion/notify-oled.py`, which sends that POST.

`stop` is the hook Cursor runs when an agent turn ends (`status` is `completed`, `error`, or `aborted`). It is the right signal for "the prompt finished." `afterAgentResponse` also exists; it fires after each assistant message, including messages in the middle of a turn that is still using tools. Use `stop` unless you specifically want the face to perk up on every message.

The script reads the hook's JSON on stdin, picks a short caption, and posts:

```json
{"event":"prompt_complete","text":"New reply"}
```

The board keeps that body as posted. The smoker and yesman faces scroll it in the top overlay. An optional `"face"` field (`"idle"`, `"smoker"`, or `"yesman"`) switches which face reacts; any other value leaves the current face. See [What the face does](#what-the-face-does).

| Turn | Top-strip text |
| --- | --- |
| `stop` with `completed` | `New reply`, or `FACE_TEXT` if you set one |
| `stop` with `error` | `Hit a snag` |
| `stop` with `aborted` | `Stopped` |

The full assistant reply is never sent. The top overlay only fits about 21 characters, and the caption is just a nudge that something new happened. The script prints `{}` and does not return a `followup_message`, so this hook cannot start another agent turn. If the board is off or the IP is wrong, the script gives up in about two seconds and Cursor continues.

This is for the **Cursor desktop app** on the same LAN as the ESP32. A cloud agent does not run `~/.cursor/hooks.json`, and it cannot see your desk's LAN address.

### Where to put the hook

User hooks live in your home directory and run with working directory `~/.cursor/`. That one install covers every project. Do not also add the project copy below unless you want two posts per turn.

```bash
mkdir -p ~/.cursor/hooks
cp companion/notify-oled.py ~/.cursor/hooks/notify-oled.py
cp companion/config.env.example ~/.cursor/hooks/config.env
chmod +x ~/.cursor/hooks/notify-oled.py
```

Edit `~/.cursor/hooks/config.env` and set the IP the OLED showed:

```text
ESP32_FACE_URL=http://192.168.1.42/notify
```

If you changed `HTTP_PORT` in `include/config.h`, put that port in the URL (`http://192.168.1.42:8080/notify`).

Create or merge `~/.cursor/hooks.json`. If the file already exists, add the `stop` entry and leave your other hooks in place. A fresh file is:

```json
{
  "version": 1,
  "hooks": {
    "stop": [
      {
        "command": "python ./hooks/notify-oled.py",
        "timeout": 5
      }
    ]
  }
}
```

The same JSON is in `companion/hooks.user.json.example`. The command uses `python` (works on Windows, where Cursor runs hooks through PowerShell; stdin still reaches the script). On macOS or Linux without a `python` shim, use `python3`. Only the `stop` hook is installed: there is deliberately no prompt-submit hook, so unread is cleared only by the button or `POST /clear`.

Reload Cursor after editing `hooks.json` (quit and reopen, or start a new agent chat from a freshly launched window). Hooks are read when the app loads them.

Optional: `FACE_TEXT=All done` in `config.env` changes the caption for a normal completion only.

### One project instead of every project

Project hooks run from the repository root, and only for that repo. Point `command` at the script with an absolute path, because the working directory is the repo, not `~/.cursor/`:

```json
{
  "version": 1,
  "hooks": {
    "stop": [
      {
        "command": "python /home/YOU/.cursor/hooks/notify-oled.py",
        "timeout": 5
      }
    ]
  }
}
```

Save that as `<repo>/.cursor/hooks.json`. The IP still comes from `~/.cursor/hooks/config.env` (next to the script) or from `ESP32_FACE_URL` in the environment.

To perk up on every assistant message instead of the finished turn, use the same command under `afterAgentResponse`. Do not register both for the same script unless you want two alerts per turn.

## Try it without Cursor

From the computer, after the IP has appeared on the panel:

```bash
curl -s -X POST http://192.168.1.42/notify \
  -H 'Content-Type: application/json' \
  -d '{"event":"prompt_complete","text":"New reply"}'
```

An empty POST also alerts, with the default caption:

```bash
curl -s -X POST http://192.168.1.42/notify
```

Check the listener:

```bash
curl -s http://192.168.1.42/health
```

Dry-run the companion (prints the POST on stderr, sends nothing, writes `{}` on stdout):

```bash
python3 companion/notify-oled.py --dry-run --text "New reply"
printf '%s' '{"hook_event_name":"stop","status":"completed","loop_count":0}' \
  | python3 companion/notify-oled.py --dry-run --verbose
```

Clear the count: `curl -s -X POST http://192.168.1.42/clear`.

The board is an open HTTP listener on your LAN. Anyone who can reach that IP can trigger the face. There is no auth token.

## What the face does

The panel can show `idle`, `smoker`, or `yesman`. `BOOT_FACE` in `include/config.h` chooses the one drawn at power-on (the file ships as `idle`). A finished-turn POST can switch without reflashing:

```bash
curl -s -X POST http://192.168.1.42/notify \
  -H 'Content-Type: application/json' \
  -d '{"event":"prompt_complete","text":"New reply","face":"smoker"}'
```

`"face":"idle"`, `"face":"smoker"`, and `"face":"yesman"` are the stable ids. Any other value leaves the current face in place, and the notify still plays on that face. The companion can keep posting its usual body; the board uses whatever arrived. Bodies larger than 4KB are refused. Anything accepted is kept whole.

Every face keeps moving, and all three share the same states, driven by the unread count and notifications:

| State | When | Look |
| --- | --- | --- |
| `SLEEPING` | unread is 0 (also at boot) | Eyes closed, slow breathing, little drifting `z` letters, minimal motion. `smoker`: cigarette resting and dim, no smoking sequence. `yesman`: calm closed-eye look, no scanline or flicker. |
| `AWAKE` | unread > 0 | Eyes open with the normal idle animation (blinks, glances, breathing). `smoker` holds the cigarette and takes an occasional drag. |
| `TALKING` | a new notification, for the alert duration | Mouth moving, brows perked, faster motion. `smoker` keeps its inhale / exhale / puff sequence. Settles to `AWAKE` while unread > 0. |

Transitions: a notification while asleep first shows a brief (about 0.8 s) wake-up with the eyes opening, then talks. Clearing unread (button on GPIO 32 or `POST /clear`) cancels any alert and the eyes droop closed into `SLEEPING`. The state is a shared `FaceState` enum (`src/face_state.h`) passed to every face's `updateAndDraw()`.

### `idle`

The original face. It does not play the cigarette sequence.

- **Waiting:** slow breathing, a blink every few seconds, and an occasional glance. A small pupil wobble keeps running between those.
- **Alert:** on `prompt_complete`, the eyes ease wider, the brows lift, the smile grows, and the motion continues (including the odd blink). The caption (`New reply`, the `text` field, or `DEFAULT_ALERT_TEXT`) shows in the top overlay. After `ALERT_HOLD_MS` the face eases back and the caption clears.

### `smoker`

Hi-tech line art, white on black: sharp shades, a hard jaw, a lit cigarette. Smoke, the ember, and a visor scan keep moving while he waits.

On `prompt_complete`, in order:

1. He inhales on the cigarette (the ember flares, the shades narrow, smoke pulls toward the mouth).
2. He takes the cigarette out.
3. He exhales a visible puff.
4. A bubble icon appears under the top overlay, which scrolls `got a new reply` plus the JSON body.
5. The icon and scroll end after the hold, the cigarette returns, the cigarette comes back, and the idle smoking loop resumes.

### `yesman`

Face id `yesman`. An original screen-shaped head, white on black: a monitor bezel, round eyes, and a wide toothy grin. It is not copied from game art. It never holds a still frame — the eyes blink, one corner of the grin twitches, a scanline walks the screen, and the glass flickers.

On `prompt_complete` he perks up, the grin eases wider, and a bubble icon shows while the top overlay scrolls `got a new reply` plus the JSON body. When the hold ends, the grin settles back into the idle loop.

```bash
curl -s -X POST http://192.168.1.42/notify \
  -H 'Content-Type: application/json' \
  -d '{"event":"prompt_complete","face":"yesman","text":"New reply"}'
```

Boot stays on `BOOT_FACE`. That value is `idle` unless you change it. `yesman` comes up only when a notify names it, or when `BOOT_FACE` is set to `yesman`.

### Boot

The selected face is already moving while Wi-Fi connects. The IP replaces the "Joining WiFi" line once the board has an address. The status shows briefly in the top overlay, whichever face is showing.

## Testing the button

Open the serial monitor (`pio device monitor -b 115200`). At boot you should see `Button GPIO 32 reads HIGH`; if it says LOW the button is wired to 3V3 or the pin is shorted. Each press logs `Button raw LOW (pressed)` then `Unread cleared by button`. `curl -s http://IP/health` shows `"button":1` while the button is held. To rule out the button, `curl -s -X POST http://IP/clear` should zero the count.
