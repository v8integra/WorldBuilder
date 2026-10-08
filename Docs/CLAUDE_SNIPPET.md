# AI World Builder — CLAUDE.md snippet

Paste this into the `CLAUDE.md` of an Unreal project that has the AI World Builder plugin.

```markdown
## World building (AI World Builder plugin, via the Unreal MCP server)

- Start every world-building session with `AIWorldBuilderToolsets.WorldBuilderDiagnosticsToolset.DescribeWorld`, and read
  the "AIWorldBuilder workflow" agent skill (AgentSkillToolset.GetSkills) before building.
- Workflow: inspect → plan (coordinates in meters, check the landscape's height limits) → create/sculpt → capture and
  look at the PNGs → paint (PaintByRules) → scatter (ScatterFoliage / PCG) → capture → TraceGround to verify collision.
- Shape terrain only with the landscape tools. Never approximate terrain features with static meshes; if a tool is
  missing, say so.
- All tool arguments are meters and degrees. Every call is undoable; AI work lives in the "AI Sculpt" and "AI Paint"
  edit layers.
- Heavy trees (Fab/Quixel Megaplants) can hang the GPU: scatter at 50–150 per hectare on small regions and capture
  before extending.
- Remind the user to Save All after creating assets (layer infos, foliage types, converted meshes).
```
