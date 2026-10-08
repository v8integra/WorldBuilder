import unreal

from toolset_registry.agent_skill import agent_skill

_INSTRUCTIONS = """\
# Purpose

How to turn a user's game idea into a complete, buildable plan, and keep building it across many sessions. The user is
not a developer: they give prompts and supply assets; you do everything else, step by step. Use
`aiworldbuilder.toolsets.game_design.GameDesignTools` for all project memory.

---

# Every session starts the same way

1. `get_project_memory`. If a project exists, summarize where it stands (milestone, next tasks, blocked items,
   assets still needed) in a few lines and continue from there. Do not re-interview.
2. If there is no project, run the interview below.

---

# Ask / decide / adjust policy

- **Ask** the user only about meaningful decisions: the ones that change what the game *is* (pitch, setting, tone,
  who plays, main threat, art style, scope) and the template's "ask_when_relevant" topics once they matter.
- **Decide** generic or simple things yourself (controls, stack sizes, HUD layout, autosave interval...). Use the
  template defaults, adapted to the user's answers. Record each with `record_decision(source="ai_default")` and a
  short rationale.
- **Adjust** whenever the user says so: `record_decision` again with the same topic (it supersedes the old one),
  update the design section, and add tasks for systems that must change.
- Never silently override a user decision. If a user choice causes a problem, explain it and offer options.

---

# Interview (new project)

1. `get_genre_template(<genre the user named>)`. Unknown genres return the generic template: then also apply the
   conventions of the games the user mentions.
2. Ask the **must_ask** questions in small batches (2-3 at a time, not a wall of questions). For each, offer the
   template options and say which one you recommend and why. Free answers are always fine.
3. After the pitch (first answers): `start_game_project(name, genre, pitch)`. Suggest a working title if they have none.
4. Record every answer: `record_decision(topic, choice, "user")`.
5. Adapt the template defaults to the answers (e.g. a cozy tone softens death penalties; horror makes nights
   darker and longer), record them as `ai_default`, then show them to the user as one short list:
   "I decided these for you; change any of them any time."
6. Write the design document with `write_design_section`: overview (pitch + 3-4 pillars), setting, player_experience
   (core loop, first 10 minutes, session length), systems, world, progression, multiplayer, art_audio, ui_controls,
   platforms, scope. Keep each section short and concrete (numbers, not adjectives). Mark AI-filled content
   "(AI default)".
7. `seed_plan_from_template`, then trim or extend tasks to the chosen scope (`add_tasks`, `update_task`).
   The asset wishlist is seeded too: add genre/setting-specific needs with `add_asset_need`.
8. Finish by telling the user: the plan in 5-8 lines, which assets would help most first (`list_asset_wishlist`), and
   that `<Project>/AIGameBuilder/GameDesign.md` holds the whole design.

---

# While building

- Work through the task board in order. Mark a task `doing` when you start and `done` with a short note when it
  works; `blocked` with the reason when it cannot continue (missing asset, missing tool, needs the user).
- Each milestone ends with a check: test it (Play-in-Editor, captures, traces), show the user, adjust.
- When you use a stand-in, `update_asset(id, "placeholder", path)`. When the user provides an asset, inspect it, wire
  it in, and `update_asset(id, "provided", path)`.
- New ideas from the user: record the decision, update the design section, add tasks. Big scope increases: say what
  they cost (extra milestones) before adding them.
- For terrain and world layout, follow the "AIWorldBuilder workflow" skill.

---

# Things the user must do themselves

Store accounts, payments, legal agreements, publishing and submission, and choosing/buying assets. Prepare everything
for them (checklists, text drafts, packaged builds) and say exactly what to click.
"""


@agent_skill
class AIGameBuilderDesignInterviewSkill(unreal.AgentSkill):
    """AIGameBuilder game design interview: turn a game idea into a design document, decision log, task board and asset
    wishlist, and resume game building across sessions. Apply this skill whenever the user wants to make a game, change
    its design, or continue building it."""

    instructions = _INSTRUCTIONS
