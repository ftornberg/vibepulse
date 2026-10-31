"""Strict Codex-to-panel interaction normalization.

Codex MCP questions and permission hooks use different wire formats from the
existing Claude hooks.  This module validates their small supported subset and
separates private adapter data from the bounded panel ``view``.
"""

from __future__ import annotations

import unicodedata
from typing import Any, Dict, Optional

if __package__:
    from .agent_status import sanitize_project
    from .interactions import (approval_view, approvable_tool,
                               shell_command_is_safe)
else:  # direct execution, same convention as tokenserver.py
    from agent_status import sanitize_project
    from interactions import (approval_view, approvable_tool,
                              shell_command_is_safe)


_QUESTION_FIELDS = frozenset({"question", "header", "options"})
_OPTION_FIELDS = frozenset({"label", "description", "recommended"})
def _is_control_free(value: str) -> bool:
    try:
        value.encode("utf-8")
    except UnicodeEncodeError:
        return False
    return not any(unicodedata.category(char).startswith("C")
                   for char in value)


def _text_is_valid(value: Any, maximum_bytes: Optional[int] = None) -> bool:
    """Whether a panel-bound text value needs no cleanup or truncation."""
    if not isinstance(value, str) or not value.strip():
        return False
    if not _is_control_free(value):
        return False
    encoded = value.encode("utf-8")
    return maximum_bytes is None or len(encoded) <= maximum_bytes


# The strict shell classifier now lives in interactions.py so Claude and
# Codex approvals are judged by the same shapes; the old name stays for
# the explicit belt-and-braces call below.
_codex_shell_command_is_safe = shell_command_is_safe


def _identity(cwd: Any, session_id: Any,
              turn_id: Any) -> Optional[Dict[str, Any]]:
    if not all(_text_is_valid(value) for value in (cwd, session_id, turn_id)):
        return None
    return {
        "project": sanitize_project(cwd),
        "session_id": session_id,
        "turn_id": turn_id,
    }


def normalize_codex_question(payload: dict, *, cwd: str,
                             session_id: str,
                             turn_id: str) -> Optional[dict]:
    """Normalize a supported Codex MCP question without guessing an answer."""
    identity = _identity(cwd, session_id, turn_id)
    if identity is None or not isinstance(payload, dict):
        return None
    if set(payload) - _QUESTION_FIELDS or \
            not {"question", "options"}.issubset(payload):
        return None

    prompt = payload["question"]
    if not _text_is_valid(prompt, 96):
        return None
    if "header" in payload and not _text_is_valid(payload["header"], 64):
        return None
    raw_options = payload["options"]
    if not isinstance(raw_options, list) or len(raw_options) not in (2, 3):
        return None

    options = []
    recommended = None
    for index, raw_option in enumerate(raw_options):
        if not isinstance(raw_option, dict) or \
                set(raw_option) - _OPTION_FIELDS or \
                "label" not in raw_option:
            return None
        label = raw_option["label"]
        if not _text_is_valid(label, 64):
            return None
        normalized_option = {"label": label}
        if "description" in raw_option:
            description = raw_option["description"]
            if not _text_is_valid(description, 64):
                return None
            normalized_option["description"] = description
        if "recommended" in raw_option:
            if not isinstance(raw_option["recommended"], bool):
                return None
            normalized_option["recommended"] = raw_option["recommended"]
            if raw_option["recommended"]:
                if recommended is not None:
                    return None
                recommended = index
        options.append(normalized_option)

    view: Dict[str, Any] = {
        "kind": "question",
        "options_total": len(options),
        "marked": recommended is not None,
        "prompt": prompt,
        "can_approve": recommended is not None,
    }
    if recommended is not None:
        selected = options[recommended]
        view["title"] = selected["label"]
        view["subtitle"] = selected.get("description")

    return {
        "provider": "codex",
        "kind": "question",
        **identity,
        "options": options,
        "recommended_index": recommended,
        "view": view,
    }


def normalize_codex_permission(event: dict, *, reveal: bool) -> Optional[dict]:
    """Normalize one Codex permission hook event for the panel."""
    if not isinstance(event, dict) or \
            event.get("hook_event_name") != "PermissionRequest":
        return None
    required = ("session_id", "turn_id", "cwd", "tool_name")
    if any(not _text_is_valid(event.get(name)) for name in required) or \
            not isinstance(event.get("tool_input"), dict):
        return None

    identity = _identity(event["cwd"], event["session_id"], event["turn_id"])
    if identity is None:
        return None
    tool_name = event["tool_name"]
    tool_input = dict(event["tool_input"])
    for field in ("command", "description"):
        value = tool_input.get(field)
        if isinstance(value, str) and not _is_control_free(value):
            return None
    view = approval_view(tool_name, tool_input, reveal)
    # Keep this explicit even though approval_view currently performs the same
    # check: the adapter boundary must not make a future view change an allow
    # path for a tool outside the established narrow classifier.
    can_approve = approvable_tool(tool_name, tool_input)
    if tool_name.strip().casefold() in {"bash", "shell"}:
        can_approve = can_approve and _codex_shell_command_is_safe(
            tool_input.get("command"))
    view["can_approve"] = bool(view.get("can_approve")) and can_approve
    normalized_event = {
        "hook_event_name": "PermissionRequest",
        "session_id": event["session_id"],
        "turn_id": event["turn_id"],
        "cwd": event["cwd"],
        "tool_name": tool_name,
        "tool_input": tool_input,
    }
    return {
        "provider": "codex",
        "kind": "approval",
        **identity,
        "event": normalized_event,
        "recommended_index": None,
        "view": view,
    }


def codex_permission_response(verdict: str) -> Optional[dict]:
    """Format a documented Codex PermissionRequest hook decision."""
    if verdict == "leave_it":
        return None
    if verdict == "approve":
        decision = {"behavior": "allow"}
    elif verdict == "deny":
        decision = {"behavior": "deny", "message": "Denied from VibePulse"}
    else:
        raise ValueError("unsupported permission verdict")
    return {"hookSpecificOutput": {
        "hookEventName": "PermissionRequest", "decision": decision,
    }}


def codex_question_result(verdict: str, normalized: dict) -> dict:
    """Answer only a Codex option that was explicitly recommended."""
    index = normalized.get("recommended_index")
    if verdict != "approve" or index is None:
        return {"status": "computer", "reason": verdict}
    option = normalized["options"][index]
    return {"status": "answered", "option_index": index,
            "answer": option["label"]}
