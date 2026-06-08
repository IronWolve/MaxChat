"""Helpers for importing saved server-list data without stale bundled servers."""

from __future__ import annotations

import copy
from collections.abc import Iterable, Mapping
from typing import Any


CATALOG_NETWORK_FIELDS = frozenset({"name", "host", "port", "tls", "website", "servers"})


def _network_key(net: Mapping[str, Any]) -> str:
    name = str(net.get("name") or "").strip().casefold()
    if name:
        return name
    return str(net.get("host") or "").strip().casefold()


def _network_dicts(value: object) -> list[dict[str, Any]]:
    if not isinstance(value, Iterable) or isinstance(value, (str, bytes)):
        return []
    return [copy.deepcopy(dict(item)) for item in value if isinstance(item, Mapping)]


def merge_imported_networks(
    imported_networks: object,
    base_networks: object,
) -> list[dict[str, Any]]:
    """Merge a backup's personal network fields into the current bundled catalog.

    Backup imports should not replace MaxChat's current server/homepage catalog with an old one.
    Matching network groups keep the current catalog fields and restore user-owned fields such as
    nick, account, passwords, startup channels, perform commands, and proxy settings. Custom
    networks from the backup are kept whole.
    """
    imported = _network_dicts(imported_networks)
    base = _network_dicts(base_networks)
    base_by_key = {
        key: net
        for net in base
        if (key := _network_key(net))
    }

    merged: list[dict[str, Any]] = []
    used_base_keys: set[str] = set()
    for imported_net in imported:
        key = _network_key(imported_net)
        if key and key in base_by_key and key not in used_base_keys:
            net = copy.deepcopy(base_by_key[key])
            for field, value in imported_net.items():
                if field not in CATALOG_NETWORK_FIELDS:
                    net[field] = copy.deepcopy(value)
            merged.append(net)
            used_base_keys.add(key)
        else:
            merged.append(copy.deepcopy(imported_net))

    imported_keys = set(used_base_keys)
    for base_net in base:
        key = _network_key(base_net)
        if key and key in imported_keys:
            continue
        merged.append(copy.deepcopy(base_net))

    return merged
