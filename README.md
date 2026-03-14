# bluetoothReceiver

`bluetoothReceiver` is an openFrameworks UI for a Raspberry Pi audio receiver.

It renders a simple now-playing screen for the **currently active Bluetooth or AirPlay session** and shows:

- active device / user name
- song title
- artist
- album
- playback status
- playback volume
- album artwork

The project is split into two layers:

1. `scripts/receiver_state_bridge.py`
   - collects metadata from local Pi services
   - normalizes it into a JSON state file
   - caches album artwork locally

2. the openFrameworks app
   - polls the JSON state file
   - loads cached artwork
   - renders the UI fullscreen-style on a connected display

This README covers installation, configuration, the local JSON API, and how to run both pieces.

## Repository layout

```text
bluetoothReceiver/
├── bin/data/receiver_state.json       # JSON state written by the bridge and read by the UI
├── scripts/receiver_state_bridge.py   # metadata bridge / local API producer
├── src/ofApp.cpp                      # now-playing UI
├── src/ReceiverState.h                # JSON state contract on the C++ side
└── README.md
```

## Architecture

### Data flow

```text
Bluetooth device / AirPlay sender
            │
            ▼
  BlueZ + shairport-sync metadata
            │
            ▼
scripts/receiver_state_bridge.py
            │
            ├── writes bin/data/receiver_state.json
            └── stores artwork in bin/data/artwork-cache/
            │
            ▼
      openFrameworks UI
```

### Bluetooth path

The bridge uses:

- `bluetoothctl devices Connected` to find the connected device
- `busctl` against BlueZ to read:
  - `org.bluez.Device1`
  - `org.bluez.MediaPlayer1`
  - `org.bluez.MediaTransport1`

### AirPlay path

The bridge reads a `shairport-sync` metadata text stream and extracts:

- client / session name
- title
- artist
- album
- volume
- embedded cover art (`PICT`) when available

If embedded artwork is not available, the bridge falls back to internet lookup and local caching.

## What "API" means in this project

This project does **not** expose an HTTP API.

The local API is a **JSON state file** written by the bridge:

```text
bin/data/receiver_state.json
```

The openFrameworks app consumes that file as its source of truth.

If you later want to add a web server or remote control layer, this JSON contract is the best starting point.

## Prerequisites

### Hardware

- Raspberry Pi 2
- display connected to the Pi
- Bluetooth audio source device for Bluetooth testing
- AirPlay sender for AirPlay testing

### OS

- Raspberry Pi OS 12 (Bookworm) is the intended target

### Software

- openFrameworks installed on the Pi
- Python 3
- `bluetoothctl`
- `busctl`
- BlueZ with Bluetooth receiver support
- `shairport-sync` if you want AirPlay metadata

## Installing the receiver services

This repository does not install BlueZ, ALSA, or AirPlay services by itself.

The implementation was designed around the setup pattern used in:

`https://github.com/nicokaiser/rpi-audio-receiver/blob/main/install.sh`

At a high level, you should install and enable:

- Bluetooth receiver support via BlueZ / ALSA
- `shairport-sync` if you want AirPlay support

If you use the reference installer, make sure the relevant components are enabled for your target:

- Bluetooth Audio
- Shairport Sync

## Configuring `shairport-sync` metadata

For AirPlay support, the bridge needs access to the `shairport-sync` metadata stream.

The bridge defaults to reading:

```text
/tmp/shairport-sync-metadata
```

Your `shairport-sync` configuration needs to write metadata to a matching path.

Example configuration shape:

```ini
metadata = {
    enabled = "yes";
    include_cover_art = "yes";
    pipe_name = "/tmp/shairport-sync-metadata";
};
```

Notes:

- Exact option names can vary slightly by `shairport-sync` version/build.
- If your installation uses a different metadata file or FIFO path, pass it to the bridge with `--airplay-metadata-path`.
- Embedded AirPlay cover art is used directly when present.

## Building the openFrameworks app

From the project root:

```bash
make Debug
```

To run the debug app:

```bash
make RunDebug
```

Or run the built binary directly:

```bash
cd bin/bluetoothReceiver_debug.app/Contents/MacOS/
./bluetoothReceiver_debug
```

On Raspberry Pi / Linux, your generated binary path will follow the normal openFrameworks output for that platform.

## Running the metadata bridge

From the project root:

```bash
python3 scripts/receiver_state_bridge.py
```

This runs continuously and updates:

```text
bin/data/receiver_state.json
```

### Useful flags

```bash
python3 scripts/receiver_state_bridge.py --help
```

Supported flags:

- `--output PATH`
  - override the JSON output path

- `--cache-dir PATH`
  - override the artwork cache directory

- `--airplay-metadata-path PATH`
  - point to the `shairport-sync` metadata stream or log file

- `--poll-interval SECONDS`
  - change polling cadence; default is `1.0`

- `--once`
  - collect one snapshot and exit

Example:

```bash
python3 scripts/receiver_state_bridge.py \
  --output /tmp/receiver_state.json \
  --cache-dir /tmp/receiver-art-cache \
  --airplay-metadata-path /tmp/shairport-sync-metadata \
  --poll-interval 0.5
```

## Recommended startup sequence

On the Pi, the simplest operational flow is:

1. start Bluetooth / AirPlay services
2. start the metadata bridge
3. start the openFrameworks UI

Example:

```bash
python3 scripts/receiver_state_bridge.py &
make RunRelease
```

For a real deployment, use `systemd` for the bridge and launch the UI from your kiosk/session startup.

## Optional `systemd` service for the bridge

Example unit:

```ini
[Unit]
Description=Receiver state bridge
After=bluetooth.service shairport-sync.service

[Service]
WorkingDirectory=/path/to/bluetoothReceiver
ExecStart=/usr/bin/python3 /path/to/bluetoothReceiver/scripts/receiver_state_bridge.py
Restart=always
RestartSec=2

[Install]
WantedBy=multi-user.target
```

After creating the unit:

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now receiver-state-bridge.service
```

## Local JSON API

### Default location

```text
bin/data/receiver_state.json
```

### Example payload

```json
{
  "activeUser": "Peter iPhone",
  "album": "Endless Summer",
  "artist": "The Midnight",
  "artworkPath": "/absolute/path/to/artwork.png",
  "artworkUrl": "",
  "connectionState": "connected",
  "deviceName": "Peter iPhone",
  "durationMs": 0,
  "error": "",
  "lastUpdated": "2026-03-14T21:22:08+00:00",
  "playbackStatus": "playing",
  "positionMs": 0,
  "sessionName": "Peter iPhone",
  "sourceType": "airplay",
  "title": "Night Drive",
  "volumePercent": 70.0
}
```

### Field reference

| Field | Type | Meaning |
| --- | --- | --- |
| `sourceType` | string | `idle`, `bluetooth`, or `airplay` |
| `connectionState` | string | Usually `disconnected` or `connected` |
| `playbackStatus` | string | Playback state such as `stopped`, `paused`, `playing`, or `connected` |
| `activeUser` | string | Best user-facing source label |
| `deviceName` | string | Device name from Bluetooth or AirPlay metadata |
| `sessionName` | string | Session/client name when available |
| `title` | string | Current track title |
| `artist` | string | Current artist |
| `album` | string | Current album |
| `volumePercent` | number | Normalized volume percentage; `-1` means unavailable |
| `positionMs` | integer | Playback position in milliseconds when available |
| `durationMs` | integer | Track duration in milliseconds when available |
| `artworkPath` | string | Local cached artwork file path |
| `artworkUrl` | string | Remote artwork URL used for caching when applicable |
| `lastUpdated` | string | UTC ISO-8601 timestamp of the latest bridge update |
| `error` | string | Human-readable bridge error message; empty when healthy |

### Contract notes

- `sourceType: "idle"` means no active session is currently detected.
- `volumePercent: -1` means volume information was unavailable from the active source.
- `artworkPath` is preferred by the UI because it avoids blocking on network fetches.
- `error` is intended for visible diagnostics, not just logs.

## Artwork behavior

Artwork is resolved in this order:

1. embedded AirPlay cover art from `PICT`
2. cached artwork already stored locally
3. internet lookup using track metadata

By default, internet artwork is cached under:

```text
bin/data/artwork-cache/
```

This keeps the UI responsive and avoids repeated downloads.

## UI behavior

The openFrameworks app:

- polls `receiver_state.json` every 500 ms
- reloads artwork when the path changes
- shows a disconnected / waiting state when there is no active session
- renders bridge errors visibly on screen

Keyboard shortcuts:

- `R` reloads the JSON state file immediately
- `F` toggles fullscreen

## Troubleshooting

### The UI says it is waiting for metadata

Check:

- is the bridge running?
- is `bin/data/receiver_state.json` being updated?
- does the file contain `sourceType: "bluetooth"` or `sourceType: "airplay"`?

Try:

```bash
python3 scripts/receiver_state_bridge.py --once
cat bin/data/receiver_state.json
```

### Bluetooth device is connected but title/artist/album are missing

Possible causes:

- the sender is not exposing AVRCP metadata
- the BlueZ media player object is not present for that device
- `busctl` is unavailable

Check:

```bash
bluetoothctl devices Connected
busctl tree org.bluez
```

### AirPlay metadata is missing

Check:

- `shairport-sync` is running
- metadata output is enabled
- the metadata path matches `--airplay-metadata-path`

### Volume shows `Unknown`

That means the active source did not expose a usable volume value.

- Bluetooth volume depends on `org.bluez.MediaTransport1`
- AirPlay volume is derived from the `pvol` metadata event

### Artwork is missing

Check:

- whether AirPlay sent embedded cover art
- whether the Pi has network access for internet artwork lookup
- whether files are being written under `bin/data/artwork-cache/`

## Development notes

### What has been verified

- the app builds successfully with `make Debug`
- the bridge compiles with `python3 -m py_compile`
- the bridge was exercised with a synthetic AirPlay metadata stream, including cover art

### What still needs on-device verification

This repository still needs final target-hardware validation on a real Raspberry Pi 2:

- live Bluetooth AVRCP metadata with your actual phone/media apps
- live `shairport-sync` metadata on your installed build
- reconnect behavior
- long-running stability
- kiosk/fullscreen startup on the Pi

## Future extension ideas

- add Spotify Connect as another source adapter
- expose the JSON state over HTTP or WebSocket
- add progress bar / elapsed time rendering
- add theming and screen burn-in protection

