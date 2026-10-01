"""AI World Builder - Python toolsets for building worlds through Unreal MCP."""

import json
import pathlib

_UPLUGIN_PATH = pathlib.Path(__file__).resolve().parents[3] / 'AIWorldBuilder.uplugin'


def get_plugin_version() -> str:
    """Returns VersionName from AIWorldBuilder.uplugin, the plugin's single source of truth."""
    try:
        return json.loads(_UPLUGIN_PATH.read_text(encoding='utf-8'))['VersionName']
    except (OSError, ValueError, KeyError):
        return 'unknown'
