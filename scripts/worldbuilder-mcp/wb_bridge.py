"""TCP client for the WorldBuilder MCP command bridge.

WorldBuilderZH.exe started with ``-mcp`` (or ``-mcpport:N``) listens on 127.0.0.1 and accepts one
JSON object per line: ``{"id": 1, "cmd": "map.info", "args": {}}``. It answers with
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
    """Raised when WorldBuilder rejects a command or cannot be reached."""


class WorldBuilderBridge:
    def __init__(self, host: str | None = None, port: int | None = None, timeout: float = 150.0):
        self.host = host or os.environ.get("WB_MCP_HOST", DEFAULT_HOST)
        self.port = int(port or os.environ.get("WB_MCP_PORT", DEFAULT_PORT))
        self.timeout = timeout
        self._sock: socket.socket | None = None
        self._reader = None
        self._lock = threading.Lock()
        self._ids = itertools.count(1)

    def _connect(self) -> None:
        try:
            sock = socket.create_connection((self.host, self.port), timeout=5.0)
        except OSError as exc:
            raise WorldBuilderError(
                f"Cannot reach WorldBuilder on {self.host}:{self.port} ({exc}). "
                "Start WorldBuilderZH.exe with the -mcp command line flag."
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
            raise ConnectionError("WorldBuilder closed the connection")
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
                # WorldBuilder may have been restarted; reconnect once.
                self.close()
                try:
                    reply = self._roundtrip(line)
                except (OSError, ConnectionError) as exc:
                    self.close()
                    raise WorldBuilderError(f"Lost connection to WorldBuilder: {exc}") from exc
        message = json.loads(reply)
        if not message.get("ok"):
            raise WorldBuilderError(message.get("error", "unknown error"))
        return message.get("result")
