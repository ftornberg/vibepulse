"""Merge queue: pull requests that are reviewed and wait for a human to merge.

An agent-team orchestrator (one per project, each on its own loopback port)
reviews pull requests and exposes the ones only a person can finish at
``GET /api/merge-queue``. This monitor polls those local endpoints and
publishes one flat LAN payload, so the panel can show "ready to merge" as
its own thing -- apart from a Claude session that is waiting, which an agent
can still take further.

Loopback only, on purpose: the orchestrators bind 127.0.0.1, and a source
list that could point anywhere would make the tokenserver a fetcher of
arbitrary URLs. Failure-isolated like the GitHub monitor: a source that is
down never takes the others with it, and its pull requests are left out
rather than shown from a stale copy (``incomplete`` says so).
"""

from __future__ import annotations

import json
import logging
import posixpath
import re
import threading
import time
import urllib.request
from typing import Any, Callable, Dict, List, Optional, Sequence


log = logging.getLogger("tokenserver.merge_queue")

POLL_SECONDS = 60.0
TIMEOUT_SECONDS = 3.0
MAX_RESPONSE_BYTES = 256 * 1024
PUBLIC_PR_LIMIT = 8
TITLE_MAX_CHARS = 80
_SOURCE_RE = re.compile(r"http://(?:127\.0\.0\.1|localhost):([0-9]{1,5})\Z")
_REPO_RE = re.compile(r"[A-Za-z0-9_.-]{1,100}/[A-Za-z0-9_.-]{1,100}\Z")


def normalize_source(value: str) -> str:
    """A bare port or a loopback origin, as ``http://127.0.0.1:<port>``."""
    value = (value or "").strip().rstrip("/")
    if value.isdigit():
        value = f"http://127.0.0.1:{value}"
    match = _SOURCE_RE.fullmatch(value)
    if match is None or not 1 <= int(match.group(1)) <= 65535:
        raise ValueError(
            "a merge-queue source is a port or http://127.0.0.1:<port>")
    return f"http://127.0.0.1:{int(match.group(1))}"


def _bounded_title(value: Any) -> Optional[str]:
    if not isinstance(value, str):
        return None
    title = " ".join(value.split())
    if not title:
        return None
    if len(title) > TITLE_MAX_CHARS:
        title = title[:TITLE_MAX_CHARS - 1].rstrip() + "…"
    return title


def parse_source_payload(payload: Any) -> Dict[str, Any]:
    """Validate one orchestrator answer; raise ValueError on anything odd."""
    if not isinstance(payload, dict):
        raise ValueError("merge-queue answer is not an object")
    repo = payload.get("repo")
    if not isinstance(repo, str) or _REPO_RE.fullmatch(repo) is None:
        raise ValueError("merge-queue answer has no valid repo")
    prs = payload.get("prs")
    if not isinstance(prs, list):
        raise ValueError("merge-queue answer has no pull request list")
    items = []
    for pr in prs:
        if not isinstance(pr, dict):
            raise ValueError("merge-queue pull request is not an object")
        number = pr.get("number")
        if isinstance(number, bool) or not isinstance(number, int) or \
                not 0 < number < 2_147_483_647:
            raise ValueError("merge-queue pull request has no valid number")
        items.append({"number": number, "title": _bounded_title(pr.get("title"))})
    return {
        "project": posixpath.basename(repo),
        "paused": payload.get("paused") is True,
        # The orchestrator has not read its pull requests yet (just started).
        "ready": isinstance(payload.get("updatedAt"), str),
        "prs": items,
    }


class MergeQueueMonitor:
    """Poll the configured orchestrators without ever raising."""

    def __init__(self, sources: Sequence[str], *,
                 opener: Optional[Callable[..., Any]] = None,
                 clock: Callable[[], float] = time.monotonic,
                 poll_seconds: float = POLL_SECONDS):
        self.sources = tuple(normalize_source(source) for source in sources)
        self._opener = opener or urllib.request.urlopen
        self._clock = clock
        self.poll_seconds = float(poll_seconds)
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._thread: Optional[threading.Thread] = None
        self._results: Dict[str, Optional[Dict[str, Any]]] = {
            source: None for source in self.sources}
        self._polled_at: Optional[float] = None

    def _fetch(self, source: str) -> Dict[str, Any]:
        request = urllib.request.Request(
            f"{source}/api/merge-queue",
            headers={"Accept": "application/json",
                     "User-Agent": "VibePulse-merge-queue/1"})
        with self._opener(request, timeout=TIMEOUT_SECONDS) as response:
            raw = response.read(MAX_RESPONSE_BYTES + 1)
        if len(raw) > MAX_RESPONSE_BYTES:
            raise ValueError("merge-queue answer is too large")
        return parse_source_payload(json.loads(raw.decode("utf-8")))

    def poll_once(self) -> int:
        """Poll every source; returns how many answered."""
        answered = 0
        for source in self.sources:
            try:
                result = self._fetch(source)
                answered += 1
            except Exception as exc:  # down, refused, malformed: all the same
                result = None
                log.debug("merge-queue source %s unavailable: %s", source, exc)
            with self._lock:
                self._results[source] = result
        with self._lock:
            self._polled_at = self._clock()
        return answered

    def snapshot(self) -> Dict[str, Any]:
        with self._lock:
            results = dict(self._results)
            polled = self._polled_at is not None
        prs: List[Dict[str, Any]] = []
        sources = []
        for source in self.sources:
            result = results.get(source)
            up = result is not None and result["ready"]
            sources.append({
                "project": result["project"] if result else None,
                "up": up,
                "paused": bool(result and result["paused"]),
            })
            if up:
                prs.extend({"project": result["project"], **pr}
                           for pr in result["prs"])
        prs.sort(key=lambda pr: (pr["project"], pr["number"]))
        return {
            "v": 1,
            "enabled": True,
            # Counts only what answered; never a made-up zero for a source
            # that is down -- `incomplete` tells the panel to say so.
            "count": len(prs),
            "incomplete": not polled or not all(s["up"] for s in sources),
            "sources": sources,
            "prs": prs[:PUBLIC_PR_LIMIT],
        }

    def run(self) -> None:
        while not self._stop.is_set():
            self.poll_once()
            if self._stop.wait(self.poll_seconds):
                break

    def start(self) -> None:
        if self._thread and self._thread.is_alive():
            return
        self._stop.clear()
        self._thread = threading.Thread(
            target=self.run, name="merge-queue-monitor", daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=TIMEOUT_SECONDS + 1.0)


def disabled_snapshot() -> Dict[str, Any]:
    return {"v": 1, "enabled": False}
