"""Minimal HTTP server for the scenario viewer.

The viewer is plain HTML/JS that works fine from ``file://`` for local use,
but serving it lets us open it cross-platform via the user's default
browser without copy-pasting paths, and is the path we'll reach for when
PRE Buddy embeds the viewer behind ``pre buddy viewer``.

Lives in stdlib only — no Flask, no FastAPI. The whole point is to be
"works out of the box on a fresh checkout."
"""

from __future__ import annotations

import webbrowser
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


def default_viewer_dir() -> Path:
    """Return the absolute path of the bundled ``viewer/`` directory."""
    # server/pre_buddy/viewer.py  →  ../../viewer
    here = Path(__file__).resolve().parent
    return (here.parent.parent / "viewer").resolve()


def make_handler(directory: Path):
    """Return a SimpleHTTPRequestHandler bound to ``directory``.

    Wrapping the directory at handler-creation time keeps the call site
    simple and works on Python 3.10+.
    """

    class _Handler(SimpleHTTPRequestHandler):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=str(directory), **kwargs)

        def log_message(self, format: str, *args) -> None:  # quieten the log
            pass

    return _Handler


def serve(*, directory: Path | None = None, host: str = "127.0.0.1", port: int = 7750, open_browser: bool = True) -> None:
    """Serve the viewer directory until interrupted (Ctrl-C)."""
    root = directory or default_viewer_dir()
    if not (root / "index.html").exists():
        raise FileNotFoundError(f"viewer/index.html not found under {root}")

    handler = make_handler(root)
    server = ThreadingHTTPServer((host, port), handler)
    url = f"http://{host}:{port}/"
    print(f"viewer serving {root} at {url} (Ctrl-C to stop)")
    if open_browser:
        try:
            webbrowser.open_new_tab(url)
        except Exception:
            pass
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
