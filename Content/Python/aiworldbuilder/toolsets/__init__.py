"""Python ToolsetDefinition classes for AI World Builder."""

from toolset_registry.registration import Registration

from aiworldbuilder.toolsets import info

registration = Registration([
    info.WorldBuilderInfoTools,
])
