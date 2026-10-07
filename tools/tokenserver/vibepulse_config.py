"""Strict, private saved feature switches for the VibePulse host service."""

from __future__ import annotations

import json
import os
import re
import stat
import tempfile
import threading
from contextlib import contextmanager
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable, Tuple
from urllib.parse import urlsplit

try:
    import fcntl
except ImportError:  # Windows
    fcntl = None
try:
    import msvcrt
except ImportError:  # macOS/Linux
    msvcrt = None


_FIELDS = frozenset({
    "claude_interactions",
    "codex_interactions",
    "interaction_detail",
    "legacy_claude_panel_v1",
    "interaction_relay",
    "agent_status_relay",
    "interaction_relay_url",
    "interaction_mailbox",
    "agent_status_ignore",
})
_MAX_CONFIG_BYTES = 16 * 1024
_PROCESS_LOCKS = {}
_PROCESS_LOCKS_GUARD = threading.Lock()
_MAILBOX_RE = re.compile(r"vp_[A-Za-z0-9_-]{16}\Z")
AGENT_STATUS_IGNORE_LIMIT = 16
AGENT_STATUS_IGNORE_MAX_CHARS = 200


def claude_project_key(path_fragment: str) -> str:
    """A path (fragment) in the form Claude Code names its project folders.

    Claude Code stores a session under ~/.claude/projects/<cwd with every
    character that is not a letter or digit replaced by "-">. Comparing in
    that form is what lets an ignore entry written as a real path
    ("/agent-team-orchestrator/work/") match the folder name on disk.
    """
    return re.sub(r"[^A-Za-z0-9]", "-", path_fragment)


def _valid_ignore_entry(value: object) -> bool:
    if not isinstance(value, str) or not value or \
            len(value) > AGENT_STATUS_IGNORE_MAX_CHARS:
        return False
    if any(ord(char) < 0x20 or ord(char) == 0x7f for char in value):
        return False
    # "/" alone would encode to "-" and hide every session on the machine.
    return sum(char.isalnum() for char in claude_project_key(value)) >= 3


class ConfigError(ValueError):
    """The saved configuration could not be trusted or persisted."""


class _ConfigLockCoordinator:
    def __init__(self) -> None:
        self.process_lock = threading.RLock()
        self.local = threading.local()


@dataclass(frozen=True)
class VibePulseConfig:
    claude_interactions: bool = False
    codex_interactions: bool = False
    interaction_detail: bool = False
    legacy_claude_panel_v1: bool = False
    interaction_relay: bool = False
    agent_status_relay: bool = False
    interaction_relay_url: str | None = None
    interaction_mailbox: str | None = None
    # Paths whose Claude sessions are left off the agent monitor — typically
    # automation that runs Claude headless (an orchestrator's work folder),
    # whose finished runs would otherwise read as "waiting" on the panel.
    agent_status_ignore: Tuple[str, ...] = ()

    def __post_init__(self) -> None:
        ignore = self.agent_status_ignore
        if isinstance(ignore, list):
            ignore = tuple(ignore)
            object.__setattr__(self, "agent_status_ignore", ignore)
        if not isinstance(ignore, tuple) or \
                len(ignore) > AGENT_STATUS_IGNORE_LIMIT or \
                not all(_valid_ignore_entry(entry) for entry in ignore) or \
                len(set(ignore)) != len(ignore):
            raise ConfigError("agent_status_ignore is invalid")
        for field_name in (
                "claude_interactions", "codex_interactions",
                "interaction_detail", "legacy_claude_panel_v1",
                "interaction_relay", "agent_status_relay"):
            if type(getattr(self, field_name)) is not bool:
                raise ConfigError(
                    f"{field_name} must be a boolean")
        if self.interaction_relay_url is not None:
            if not isinstance(self.interaction_relay_url, str) or \
                    not self.interaction_relay_url or \
                    any(ord(char) < 0x21 or ord(char) > 0x7e
                        for char in self.interaction_relay_url):
                raise ConfigError("interaction_relay_url must be HTTPS")
            parsed = urlsplit(self.interaction_relay_url)
            try:
                port = parsed.port or 443
            except ValueError as exc:
                raise ConfigError(
                    "interaction_relay_url has an invalid port") from exc
            if parsed.scheme != "https" or not parsed.hostname or \
                    parsed.username is not None or \
                    parsed.password is not None or \
                    parsed.path not in ("", "/") or parsed.query or \
                    parsed.fragment or not (1 <= port <= 65535):
                raise ConfigError("interaction_relay_url must be an origin")
        if self.interaction_mailbox is not None and (
                not isinstance(self.interaction_mailbox, str) or
                _MAILBOX_RE.fullmatch(self.interaction_mailbox) is None):
            raise ConfigError("interaction_mailbox is invalid")


def _strict_object(pairs: Iterable[Tuple[str, object]]) -> dict:
    result = {}
    for key, value in pairs:
        if key in result:
            raise ConfigError(f"duplicate configuration key: {key}")
        result[key] = value
    return result


def _same_file(first, second) -> bool:
    try:
        return os.path.samestat(first, second)
    except (AttributeError, OSError):
        return (getattr(first, "st_dev", None),
                getattr(first, "st_ino", None)) == (
                    getattr(second, "st_dev", None),
                    getattr(second, "st_ino", None))


def _safe_open_existing(path: Path) -> int:
    """Open one unchanged, non-symlink path without blocking on file kinds."""
    before = os.lstat(path)
    if stat.S_ISLNK(before.st_mode):
        raise ConfigError("configuration path must not be a symbolic link")
    flags = os.O_RDONLY
    flags |= getattr(os, "O_CLOEXEC", 0)
    flags |= getattr(os, "O_NOFOLLOW", 0)
    flags |= getattr(os, "O_NONBLOCK", 0)
    fd = os.open(path, flags)
    try:
        descriptor = os.fstat(fd)
        after = os.lstat(path)
        if (stat.S_ISLNK(after.st_mode) or
                not _same_file(before, descriptor) or
                not _same_file(descriptor, after)):
            raise ConfigError("configuration path changed while opening")
        if not stat.S_ISREG(descriptor.st_mode):
            raise ConfigError("configuration path must be a regular file")
        return fd
    except BaseException:
        os.close(fd)
        raise


def load_config(path: Path) -> VibePulseConfig:
    """Load a strict config file; a genuinely missing file means all off."""
    path = Path(path)
    fd = None
    try:
        fd = _safe_open_existing(path)
        with os.fdopen(fd, "rb") as handle:
            fd = None
            raw_bytes = handle.read(_MAX_CONFIG_BYTES + 1)
        if len(raw_bytes) > _MAX_CONFIG_BYTES:
            raise ConfigError("configuration file is too large")
        raw = raw_bytes.decode("utf-8", errors="strict")
    except FileNotFoundError:
        return VibePulseConfig()
    except ConfigError:
        raise
    except (OSError, UnicodeError) as exc:
        raise ConfigError("cannot read configuration") from exc
    finally:
        if fd is not None:
            try:
                os.close(fd)
            except OSError:
                pass

    try:
        payload = json.loads(raw, object_pairs_hook=_strict_object)
    except ConfigError:
        raise
    except (json.JSONDecodeError, UnicodeError, ValueError,
            RecursionError) as exc:
        raise ConfigError("malformed configuration JSON") from exc
    if not isinstance(payload, dict):
        raise ConfigError("configuration must be a JSON object")
    unknown = set(payload) - _FIELDS
    if unknown:
        raise ConfigError("unknown configuration keys")
    try:
        return VibePulseConfig(**payload)
    except (TypeError, ConfigError) as exc:
        if isinstance(exc, ConfigError):
            raise
        raise ConfigError("configuration values have invalid types") from exc


def _config_lock_path(path: Path) -> Path:
    return path.with_name(f".{path.name}.lock")


def _lock_coordinator(path: Path) -> _ConfigLockCoordinator:
    key = os.path.normcase(os.path.realpath(os.path.abspath(os.fspath(path))))
    with _PROCESS_LOCKS_GUARD:
        coordinator = _PROCESS_LOCKS.get(key)
        if coordinator is None:
            coordinator = _ConfigLockCoordinator()
            _PROCESS_LOCKS[key] = coordinator
        return coordinator


def _open_lock_file(path: Path) -> int:
    directory = path.parent
    directory.mkdir(mode=0o700, parents=True, exist_ok=True)
    if os.name == "posix":
        os.chmod(directory, 0o700)
    try:
        before = os.lstat(path)
    except FileNotFoundError:
        before = None
    if before is not None and stat.S_ISLNK(before.st_mode):
        raise ConfigError("configuration lock must not be a symbolic link")
    flags = os.O_RDWR | os.O_CREAT
    flags |= getattr(os, "O_CLOEXEC", 0)
    flags |= getattr(os, "O_NOFOLLOW", 0)
    flags |= getattr(os, "O_NONBLOCK", 0)
    fd = os.open(path, flags, 0o600)
    try:
        descriptor = os.fstat(fd)
        after = os.lstat(path)
        if (not stat.S_ISREG(descriptor.st_mode) or
                stat.S_ISLNK(after.st_mode) or
                (before is not None and
                 not _same_file(before, descriptor)) or
                not _same_file(descriptor, after)):
            raise ConfigError("configuration lock path is not safe")
        if hasattr(os, "fchmod"):
            os.fchmod(fd, 0o600)
        return fd
    except BaseException:
        os.close(fd)
        raise


def _acquire_file_lock(fd: int) -> None:
    if fcntl is not None:
        fcntl.flock(fd, fcntl.LOCK_EX)
    elif msvcrt is not None:
        if os.fstat(fd).st_size == 0:
            os.write(fd, b"\0")
            os.fsync(fd)
        os.lseek(fd, 0, os.SEEK_SET)
        msvcrt.locking(fd, msvcrt.LK_LOCK, 1)
    else:
        raise ConfigError("no supported cross-process lock backend")


def _release_file_lock(fd: int) -> None:
    if fcntl is not None:
        fcntl.flock(fd, fcntl.LOCK_UN)
    elif msvcrt is not None:
        os.lseek(fd, 0, os.SEEK_SET)
        msvcrt.locking(fd, msvcrt.LK_UNLCK, 1)


@contextmanager
def config_lock(path: Path):
    """Serialize one config transaction in this process and across peers."""
    lock_path = _config_lock_path(Path(path))
    coordinator = _lock_coordinator(lock_path)
    with coordinator.process_lock:
        depth = getattr(coordinator.local, "depth", 0)
        if depth == 0:
            if fcntl is None and msvcrt is None:
                raise ConfigError("no supported cross-process lock backend")
            fd = None
            try:
                fd = _open_lock_file(lock_path)
                _acquire_file_lock(fd)
            except BaseException as exc:
                if fd is not None:
                    try:
                        os.close(fd)
                    except OSError:
                        pass
                if isinstance(exc, ConfigError):
                    raise
                if isinstance(exc, OSError):
                    raise ConfigError("cannot lock configuration") from exc
                raise
            coordinator.local.descriptor = fd
        coordinator.local.depth = depth + 1
        try:
            yield
        finally:
            coordinator.local.depth -= 1
            if coordinator.local.depth == 0:
                fd = coordinator.local.descriptor
                try:
                    _release_file_lock(fd)
                finally:
                    try:
                        os.close(fd)
                    finally:
                        del coordinator.local.descriptor
                        del coordinator.local.depth

def save_config(path: Path, config: VibePulseConfig) -> None:
    """Atomically write the public feature switches with private modes."""
    if not isinstance(config, VibePulseConfig):
        raise ConfigError("config must be VibePulseConfig")
    config.__post_init__()
    path = Path(path)
    directory = path.parent
    temporary = None
    try:
        directory.mkdir(mode=0o700, parents=True, exist_ok=True)
        if os.name == "posix":
            os.chmod(directory, 0o700)
        fd, temporary_name = tempfile.mkstemp(
            prefix=f".{path.name}.", dir=directory)
        temporary = Path(temporary_name)
        try:
            if hasattr(os, "fchmod"):
                os.fchmod(fd, 0o600)
            payload = json.dumps(
                asdict(config), sort_keys=True, separators=(",", ":"),
            ).encode("utf-8") + b"\n"
            with os.fdopen(fd, "wb") as handle:
                handle.write(payload)
                handle.flush()
                os.fsync(handle.fileno())
        except Exception:
            try:
                os.close(fd)
            except OSError:
                pass
            raise
        os.replace(temporary, path)
        temporary = None
        if os.name == "posix":
            os.chmod(path, 0o600)
        try:
            directory_fd = os.open(directory, os.O_RDONLY)
        except OSError:
            directory_fd = None
        if directory_fd is not None:
            try:
                os.fsync(directory_fd)
            finally:
                os.close(directory_fd)
    except (OSError, TypeError, ValueError) as exc:
        raise ConfigError("cannot save configuration") from exc
    finally:
        if temporary is not None:
            try:
                temporary.unlink()
            except FileNotFoundError:
                pass
            except OSError:
                pass


# Concise aliases for callers that already name the module in their import.
load = load_config
save = save_config
