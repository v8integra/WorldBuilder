import unreal

import toolset_registry

from aiworldbuilder import get_plugin_version


@unreal.uclass()
class WorldBuilderInfoTools(unreal.ToolsetDefinition):
    """Basic information about the AI World Builder plugin. Use ping to confirm the
    plugin's Python toolsets are loaded. For landscape and world status, use the
    WorldBuilderDiagnosticsToolset."""

    @toolset_registry.tool_call
    @staticmethod
    def ping() -> str:
        """Confirms the AI World Builder Python toolsets are loaded and returns the plugin version.

        Example: ping() -> "AIWorldBuilder 0.1.0"

        Returns:
            "AIWorldBuilder <version>".
        """
        return f'AIWorldBuilder {get_plugin_version()}'
