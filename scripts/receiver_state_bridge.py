#!/usr/bin/env python3

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import time
import urllib.parse
import urllib.request
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, Optional


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT_PATH = PROJECT_ROOT / "bin" / "data" / "receiver_state.json"
DEFAULT_CACHE_DIR = PROJECT_ROOT / "bin" / "data" / "artwork-cache"
DEFAULT_AIRPLAY_METADATA_PATH = Path("/tmp/shairport-sync-metadata")


def log(message: str) -> None:
    print(f"[receiver_state_bridge] {message}", file=sys.stderr, flush=True)


def utc_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat()


def clamp(value: float, lower: float, upper: float) -> float:
    return max(lower, min(upper, value))


def atomic_write_json(path: Path, payload: Dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temp_path = path.with_suffix(path.suffix + ".tmp")
    temp_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    temp_path.replace(path)


def run_command(args: list[str], timeout: float = 2.0) -> str:
    completed = subprocess.run(args, check=True, capture_output=True, text=True, timeout=timeout)
    return completed.stdout.strip()


def parse_simple_busctl_value(raw_value: str) -> Any:
    tokens = shlex.split(raw_value)
    if len(tokens) < 2:
        return None

    value_type = tokens[0]
    value_token = tokens[1]

    if value_type in {"s", "o", "g"}:
        return value_token
    if value_type == "b":
        return value_token.lower() == "true"
    if value_type in {"y", "n", "q", "i", "u", "x", "t"}:
        return int(value_token)
    if value_type == "d":
        return float(value_token)

    return value_token


def parse_busctl_variant_dict(raw_value: str) -> Dict[str, Any]:
    tokens = shlex.split(raw_value)
    if len(tokens) < 2 or tokens[0] != "a{sv}":
        return {}

    result: Dict[str, Any] = {}
    index = 2
    while index + 2 < len(tokens):
        key = tokens[index]
        value_type = tokens[index + 1]
        value_index = index + 2

        if value_type == "v" and index + 3 < len(tokens):
            value_type = tokens[index + 2]
            value_index = index + 3

        if value_index >= len(tokens):
            break

        value_token = tokens[value_index]
        if value_type == "b":
            value: Any = value_token.lower() == "true"
        elif value_type in {"y", "n", "q", "i", "u", "x", "t"}:
            value = int(value_token)
        elif value_type == "d":
            value = float(value_token)
        else:
            value = value_token

        result[key] = value
        index = value_index + 1

    return result


def bluetooth_address_to_path(address: str) -> str:
    return f"/org/bluez/hci0/dev_{address.replace(':', '_')}"


def first_non_empty(*values: str) -> str:
    for value in values:
        if value:
            return value
    return ""


def detect_image_extension(payload: bytes) -> str:
    if payload.startswith(b"\x89PNG\r\n\x1a\n"):
        return "png"
    if payload.startswith(b"\xff\xd8\xff"):
        return "jpg"
    if payload.startswith((b"GIF87a", b"GIF89a")):
        return "gif"
    return "jpg"


@dataclass
class StateCandidate:
    source_type: str
    connection_state: str = "disconnected"
    playback_status: str = "stopped"
    active_user: str = ""
    device_name: str = ""
    session_name: str = ""
    title: str = ""
    artist: str = ""
    album: str = ""
    artwork_path: str = ""
    artwork_url: str = ""
    volume_percent: float = -1.0
    position_ms: int = 0
    duration_ms: int = 0
    error: str = ""
    last_updated: str = field(default_factory=utc_now)

    def connected(self) -> bool:
        return self.connection_state == "connected"

    def to_json(self) -> Dict[str, Any]:
        return {
            "activeUser": self.active_user,
            "album": self.album,
            "artworkPath": self.artwork_path,
            "artworkUrl": self.artwork_url,
            "connectionState": self.connection_state,
            "deviceName": self.device_name,
            "durationMs": self.duration_ms,
            "error": self.error,
            "lastUpdated": self.last_updated,
            "playbackStatus": self.playback_status,
            "positionMs": self.position_ms,
            "sessionName": self.session_name,
            "sourceType": self.source_type,
            "title": self.title,
            "artist": self.artist,
            "volumePercent": round(self.volume_percent, 2) if self.volume_percent >= 0 else -1,
        }


class ArtworkResolver:
    def __init__(self, cache_dir: Path) -> None:
        self.cache_dir = cache_dir
        self.cache_dir.mkdir(parents=True, exist_ok=True)

    def ensure_artwork(self, state: StateCandidate) -> None:
        if state.artwork_path or not state.artist or not (state.album or state.title):
            return

        cache_key = hashlib.sha1(
            f"{state.source_type}|{state.artist}|{state.album}|{state.title}".encode("utf-8")
        ).hexdigest()

        for candidate in self.cache_dir.glob(f"{cache_key}.*"):
            state.artwork_path = str(candidate)
            return

        search_terms = [
            f"{state.artist} {state.album}".strip(),
            f"{state.artist} {state.title}".strip(),
        ]

        for search_term in search_terms:
            artwork_url = self._lookup_artwork_url(search_term)
            if not artwork_url:
                continue

            try:
                local_path = self._download_artwork(artwork_url, cache_key)
            except Exception as error:  # noqa: BLE001
                log(f"Artwork download failed for '{search_term}': {error}")
                continue

            state.artwork_url = artwork_url
            state.artwork_path = str(local_path)
            return

    def _lookup_artwork_url(self, search_term: str) -> str:
        encoded_term = urllib.parse.quote(search_term)
        endpoint = f"https://itunes.apple.com/search?term={encoded_term}&entity=song&limit=1"
        request = urllib.request.Request(endpoint, headers={"User-Agent": "receiver-state-bridge/1.0"})
        with urllib.request.urlopen(request, timeout=5) as response:
            payload = json.loads(response.read().decode("utf-8"))

        results = payload.get("results", [])
        if not results:
            return ""

        artwork_url = results[0].get("artworkUrl100", "")
        return artwork_url.replace("100x100bb", "600x600bb")

    def _download_artwork(self, artwork_url: str, cache_key: str) -> Path:
        request = urllib.request.Request(artwork_url, headers={"User-Agent": "receiver-state-bridge/1.0"})
        with urllib.request.urlopen(request, timeout=5) as response:
            content_type = response.headers.get_content_type()
            payload = response.read()

        extension = ".jpg"
        if content_type == "image/png":
            extension = ".png"

        local_path = self.cache_dir / f"{cache_key}{extension}"
        local_path.write_bytes(payload)
        return local_path


class BluetoothAdapter:
    def __init__(self) -> None:
        self.warned_missing_tools: set[str] = set()

    def poll(self) -> Optional[StateCandidate]:
        if shutil.which("bluetoothctl") is None:
            self._warn_once("bluetoothctl", "bluetoothctl is not installed; Bluetooth metadata is unavailable.")
            return None

        try:
            connected_devices = self._connected_devices()
        except Exception as error:  # noqa: BLE001
            candidate = StateCandidate(source_type="bluetooth")
            candidate.error = f"Unable to query connected Bluetooth devices: {error}"
            return candidate

        if not connected_devices:
            return None

        address, fallback_name = connected_devices[0]
        candidate = StateCandidate(source_type="bluetooth", connection_state="connected", playback_status="connected")
        candidate.device_name = fallback_name
        candidate.active_user = fallback_name
        candidate.session_name = fallback_name

        if shutil.which("busctl") is None:
            self._warn_once("busctl", "busctl is not installed; Bluetooth track metadata will be limited.")
            return candidate

        device_path = bluetooth_address_to_path(address)
        try:
            tree_paths = self._bluez_tree_paths()
        except Exception as error:  # noqa: BLE001
            candidate.error = f"Unable to read BlueZ object tree: {error}"
            return candidate

        try:
            alias = self._get_property(device_path, "org.bluez.Device1", "Alias")
            name = self._get_property(device_path, "org.bluez.Device1", "Name")
            connected = self._get_property(device_path, "org.bluez.Device1", "Connected")
            candidate.device_name = first_non_empty(alias or "", name or "", fallback_name)
            candidate.active_user = candidate.device_name
            candidate.session_name = candidate.device_name
            candidate.connection_state = "connected" if connected is not False else "disconnected"
        except Exception as error:  # noqa: BLE001
            candidate.error = f"BlueZ device query failed: {error}"
            return candidate

        player_path = next((path for path in tree_paths if path.startswith(device_path + "/player")), "")
        if player_path:
            try:
                track = self._get_property(player_path, "org.bluez.MediaPlayer1", "Track", expect_dict=True) or {}
                candidate.title = str(track.get("Title", ""))
                candidate.artist = str(track.get("Artist", ""))
                candidate.album = str(track.get("Album", ""))
                candidate.duration_ms = int(track.get("Duration", 0) or 0)
                candidate.playback_status = str(
                    self._get_property(player_path, "org.bluez.MediaPlayer1", "Status") or candidate.playback_status
                )
                candidate.position_ms = int(
                    self._get_property(player_path, "org.bluez.MediaPlayer1", "Position") or 0
                )
            except Exception as error:  # noqa: BLE001
                candidate.error = f"BlueZ player query failed: {error}"
        else:
            candidate.error = "No MediaPlayer1 path found for connected Bluetooth device."

        transport_path = next((path for path in tree_paths if re.search(r"/fd\d+$", path) and path.startswith(device_path)), "")
        if transport_path:
            try:
                volume_value = self._get_property(transport_path, "org.bluez.MediaTransport1", "Volume")
                if volume_value is not None:
                    candidate.volume_percent = clamp(float(volume_value) / 1.27, 0.0, 100.0)
            except Exception as error:  # noqa: BLE001
                if not candidate.error:
                    candidate.error = f"BlueZ transport query failed: {error}"

        candidate.last_updated = utc_now()
        return candidate

    def _connected_devices(self) -> list[tuple[str, str]]:
        output = run_command(["bluetoothctl", "devices", "Connected"])
        devices: list[tuple[str, str]] = []
        for line in output.splitlines():
            parts = line.split(" ", 2)
            if len(parts) == 3 and parts[0] == "Device":
                devices.append((parts[1], parts[2]))
        return devices

    def _bluez_tree_paths(self) -> list[str]:
        output = run_command(["busctl", "tree", "org.bluez"], timeout=3.0)
        return re.findall(r"(/org/bluez/[^\s]+)", output)

    def _get_property(self, path: str, interface: str, property_name: str, expect_dict: bool = False) -> Any:
        output = run_command(
            ["busctl", "--system", "get-property", "org.bluez", path, interface, property_name],
            timeout=2.0,
        )
        if expect_dict:
            return parse_busctl_variant_dict(output)
        return parse_simple_busctl_value(output)

    def _warn_once(self, key: str, message: str) -> None:
        if key in self.warned_missing_tools:
            return
        self.warned_missing_tools.add(key)
        log(message)


class AirPlayAdapter:
    ITEM_PATTERN = re.compile(
        r"<item><type>([0-9a-fA-F]{8})</type><code>([0-9a-fA-F]{8})</code><length>(\d+)</length>"
        r"(?:\s*<data encoding=\"base64\">(.*?)</data>)?</item>",
        re.S,
    )

    def __init__(self, metadata_path: Path, cache_dir: Path) -> None:
        self.metadata_path = metadata_path
        self.cover_art_dir = cache_dir / "airplay"
        self.cover_art_dir.mkdir(parents=True, exist_ok=True)
        self.file_descriptor: Optional[int] = None
        self.buffer = ""
        self.state = StateCandidate(source_type="airplay")

    def poll(self) -> Optional[StateCandidate]:
        if not self.metadata_path.exists():
            return None

        try:
            self._read_new_data()
        except Exception as error:  # noqa: BLE001
            self.state.error = f"Unable to read AirPlay metadata: {error}"
            return self.state

        self._parse_buffer()

        if not self.state.connected() and not self.state.title and not self.state.artist and not self.state.album:
            return None

        self.state.last_updated = utc_now()
        return self.state

    def _read_new_data(self) -> None:
        if self.file_descriptor is None:
            self.file_descriptor = os.open(self.metadata_path, os.O_RDONLY | os.O_NONBLOCK)

        try:
            chunk = os.read(self.file_descriptor, 65536)
        except BlockingIOError:
            return

        if not chunk:
            return

        self.buffer += chunk.decode("utf-8", errors="ignore")

    def _parse_buffer(self) -> None:
        while True:
            match = self.ITEM_PATTERN.search(self.buffer)
            if match is None:
                if len(self.buffer) > 32768:
                    self.buffer = self.buffer[-8192:]
                return

            self.buffer = self.buffer[match.end():]
            raw_type, raw_code, _, raw_payload = match.groups()
            item_type = bytes.fromhex(raw_type).decode("ascii", errors="ignore")
            item_code = bytes.fromhex(raw_code).decode("ascii", errors="ignore")
            payload = base64.b64decode(raw_payload) if raw_payload else b""
            self._apply_item(item_type, item_code, payload)

    def _apply_item(self, item_type: str, item_code: str, payload: bytes) -> None:
        payload_text = payload.decode("utf-8", errors="ignore").strip()

        if item_type == "core":
            if item_code == "minm":
                self.state.title = payload_text
            elif item_code == "asar":
                self.state.artist = payload_text
            elif item_code == "asal":
                self.state.album = payload_text
            elif item_code == "astm" and len(payload) >= 4:
                self.state.duration_ms = int.from_bytes(payload[:4], byteorder="big")
            return

        if item_type != "ssnc":
            return

        if item_code in {"clip", "conn"}:
            self.state.connection_state = "connected"
            self.state.device_name = payload_text or self.state.device_name
        elif item_code == "snam":
            self.state.active_user = payload_text
            self.state.session_name = payload_text
            self.state.device_name = payload_text or self.state.device_name
        elif item_code in {"pbeg", "abeg", "prsm", "pres"}:
            self.state.connection_state = "connected"
            self.state.playback_status = "playing"
        elif item_code == "paus":
            self.state.connection_state = "connected"
            self.state.playback_status = "paused"
        elif item_code in {"disc", "pend", "aend"}:
            self.state.connection_state = "disconnected"
            self.state.playback_status = "stopped"
            self.state.title = ""
            self.state.artist = ""
            self.state.album = ""
            self.state.artwork_path = ""
            self.state.artwork_url = ""
        elif item_code == "pvol":
            volume_db = payload_text.split(",", 1)[0]
            try:
                self.state.volume_percent = clamp((float(volume_db) + 30.0) / 30.0 * 100.0, 0.0, 100.0)
            except ValueError:
                log(f"Unable to parse AirPlay volume '{payload_text}'.")
        elif item_code == "PICT":
            local_path = self._store_cover_art(payload)
            self.state.artwork_path = str(local_path)

    def _store_cover_art(self, payload: bytes) -> Path:
        extension = detect_image_extension(payload)
        local_path = self.cover_art_dir / f"airplay-current.{extension}"
        local_path.write_bytes(payload)
        return local_path


class ReceiverStateBridge:
    def __init__(self, output_path: Path, cache_dir: Path, airplay_metadata_path: Path, poll_interval: float) -> None:
        self.output_path = output_path
        self.artwork_resolver = ArtworkResolver(cache_dir)
        self.bluetooth = BluetoothAdapter()
        self.airplay = AirPlayAdapter(airplay_metadata_path, cache_dir)
        self.poll_interval = poll_interval

    def run(self, once: bool) -> None:
        while True:
            state = self._collect_state()
            atomic_write_json(self.output_path, state.to_json())
            if once:
                return
            time.sleep(self.poll_interval)

    def _collect_state(self) -> StateCandidate:
        try:
            bluetooth_state = self.bluetooth.poll()
        except Exception as error:  # noqa: BLE001
            bluetooth_state = StateCandidate(source_type="bluetooth", error=f"Bluetooth adapter failure: {error}")

        try:
            airplay_state = self.airplay.poll()
        except Exception as error:  # noqa: BLE001
            airplay_state = StateCandidate(source_type="airplay", error=f"AirPlay adapter failure: {error}")

        candidate = airplay_state if airplay_state and airplay_state.connected() else bluetooth_state
        if candidate is None:
            idle = StateCandidate(source_type="idle")
            return idle

        self.artwork_resolver.ensure_artwork(candidate)
        candidate.last_updated = utc_now()
        return candidate


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Collect Bluetooth and AirPlay metadata for the openFrameworks UI.")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT_PATH, help="State JSON output path.")
    parser.add_argument("--cache-dir", type=Path, default=DEFAULT_CACHE_DIR, help="Artwork cache directory.")
    parser.add_argument(
        "--airplay-metadata-path",
        type=Path,
        default=DEFAULT_AIRPLAY_METADATA_PATH,
        help="Path to the shairport-sync metadata text stream or log file.",
    )
    parser.add_argument("--poll-interval", type=float, default=1.0, help="Polling interval in seconds.")
    parser.add_argument("--once", action="store_true", help="Collect one snapshot and exit.")
    return parser


def main() -> int:
    parser = build_argument_parser()
    args = parser.parse_args()

    bridge = ReceiverStateBridge(
        output_path=args.output,
        cache_dir=args.cache_dir,
        airplay_metadata_path=args.airplay_metadata_path,
        poll_interval=args.poll_interval,
    )

    try:
        bridge.run(once=args.once)
    except subprocess.CalledProcessError as error:
        log(f"Command failed: {' '.join(error.cmd)}")
        if error.stderr:
            log(error.stderr.strip())
        return 1
    except KeyboardInterrupt:
        log("Interrupted.")
        return 0
    except Exception as error:  # noqa: BLE001
        log(f"Unhandled bridge error: {error}")
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
