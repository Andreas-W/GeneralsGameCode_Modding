"""TCP client for the MCP command bridges of WorldBuilder and the game.

WorldBuilderZH.exe (port 47800) and generalszh.exe (port 47801) started with ``-mcp`` (or
``-mcpport:N``) listen on 127.0.0.1 and accept one JSON object per line:
``{"id": 1, "cmd": "map.info", "args": {}}``. They answer with
``{"id": 1, "ok": true, "result": {...}}`` or ``{"id": 1, "ok": false, "error": "..."}``.
"""

from __future__ import annotations

import itertools
import json
import os
import socket
import threading
from typing import Any

DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 47800


class WorldBuilderError(RuntimeError):
    """Raised when WorldBuilder (or the game) rejects a command or cannot be reached."""


class WorldBuilderBridge:
    def __init__(self, host: str | None = None, port: int | None = None, timeout: float = 150.0,
                 label: str = "WorldBuilder", env_prefix: str = "WB_MCP", default_port: int = DEFAULT_PORT,
                 start_hint: str = "Start WorldBuilderZH.exe with the -mcp command line flag."):
        self.host = host or os.environ.get(f"{env_prefix}_HOST", DEFAULT_HOST)
        self.port = int(port or os.environ.get(f"{env_prefix}_PORT", default_port))
        self.timeout = timeout
        self.label = label
        self.start_hint = start_hint
        self._sock: socket.socket | None = None
        self._reader = None
        self._lock = threading.Lock()
        self._ids = itertools.count(1)

    def _connect(self) -> None:
        try:
            sock = socket.create_connection((self.host, self.port), timeout=5.0)
        except OSError as exc:
            raise WorldBuilderError(
                f"Cannot reach {self.label} on {self.host}:{self.port} ({exc}). {self.start_hint}"
            ) from exc
        sock.settimeout(self.timeout)
        self._sock = sock
        self._reader = sock.makefile("r", encoding="utf-8", newline="\n")

    def close(self) -> None:
        if self._reader is not None:
            self._reader.close()
        if self._sock is not None:
            self._sock.close()
        self._sock = None
        self._reader = None

    def _roundtrip(self, line: str) -> str:
        if self._sock is None:
            self._connect()
        assert self._sock is not None and self._reader is not None
        self._sock.sendall(line.encode("utf-8"))
        reply = self._reader.readline()
        if not reply:
            raise ConnectionError(f"{self.label} closed the connection")
        return reply

    def call(self, cmd: str, **args: Any) -> Any:
        """Runs one bridge command and returns its result. None-valued args are omitted."""
        args = {k: v for k, v in args.items() if v is not None}
        with self._lock:
            request_id = next(self._ids)
            line = json.dumps({"id": request_id, "cmd": cmd, "args": args}) + "\n"
            try:
                reply = self._roundtrip(line)
            except (OSError, ConnectionError):
                # The program may have been restarted; reconnect once.
                self.close()
                try:
                    reply = self._roundtrip(line)
                except (OSError, ConnectionError) as exc:
                    self.close()
                    raise WorldBuilderError(f"Lost connection to {self.label}: {exc}") from exc
        message = json.loads(reply)
        if not message.get("ok"):
            raise WorldBuilderError(message.get("error", "unknown error"))
        return message.get("result")
