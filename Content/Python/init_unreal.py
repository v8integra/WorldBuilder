"""Startup hook: Unreal runs every init_unreal.py found in a plugin's Content/Python folder."""

import unreal

from aiworldbuilder import skills  # noqa: F401 - importing registers the @agent_skill classes
from aiworldbuilder import toolsets

if toolsets.registration.register():
    unreal.log('AIWorldBuilder: Python toolsets and agent skills registered.')
else:
    unreal.log_warning('AIWorldBuilder: Toolset Registry unavailable, Python toolsets not registered.')
