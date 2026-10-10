import json

import unreal

import toolset_registry

from aiworldbuilder.design import store
from aiworldbuilder.design import templates


def _require_project() -> dict:
    if not store.exists():
        raise ValueError('No game project memory yet. Interview the user, then call start_game_project.')
    return store.load()


def _find(items: list, item_id: str, kind: str) -> dict:
    for item in items:
        if item['id'].lower() == item_id.strip().lower():
            return item
    raise ValueError(f'No {kind} with id "{item_id}".')


def _seed_packs(data: dict, template: dict) -> int:
    """Adds the template's asset pack suggestions, filling {setting}/{style}/{genre} from the project. Returns how many."""
    game = data['game']
    decisions = {d['topic'].lower(): d['choice'] for d in store.active_decisions(data)}
    fill = {
        'setting': decisions.get('setting', game.get('genre', '')),
        'style': decisions.get('art style', decisions.get('art_style', '')),
        'genre': game.get('genre', ''),
    }
    existing = {k['name'].lower() for k in data.setdefault('packs', [])}
    added = 0
    for pack in template.get('asset_packs', []):
        if pack['name'].lower() in existing:
            continue
        search = pack.get('search', '')
        for key, value in fill.items():
            search = search.replace('{' + key + '}', value)
        data['packs'].append({'id': store.next_id(data['packs'], 'P'), 'name': pack['name'], 'category': pack.get('category', ''),
                              'priority': pack.get('priority', 'recommended'), 'search': ' '.join(search.split()),
                              'why': pack.get('why', ''), 'requirements': pack.get('requirements', ''),
                              'covers': pack.get('covers', []), 'status': 'suggested', 'path': '', 'notes': ''})
        added += 1
    return added


def _task_line(t: dict) -> str:
    note = f" ({t['notes']})" if t.get('notes') else ''
    return f"- {t['id']} [{t['status']}] {t['title']}{note}"


@unreal.uclass()
class GameDesignTools(unreal.ToolsetDefinition):
    """Game project memory for building a whole game step by step: the design document, decision log (user choices and
    changeable AI defaults), task board and asset wishlist, stored in the project (AIGameBuilder/project.json, with a
    readable GameDesign.md). Call get_project_memory at the start of every game-building session. Genre templates say
    what to ask the user and what to decide by default. Read the "AIGameBuilder game design interview" agent skill."""

    # ------------------------------------------------------------------ overview

    @toolset_registry.tool_call
    @staticmethod
    def get_project_memory() -> str:
        """Returns a summary of the game being built: game info, design sections, decisions, task board progress with the
        next tasks, and asset wishlist status. Call this first in every session to resume where the last one stopped.

        Example: get_project_memory()

        Returns:
            Markdown summary, or instructions to start the design interview if there is no project yet.
        """
        if not store.exists():
            return ('No game project yet. Run the design interview (agent skill "AIGameBuilder game design interview"): '
                    'get_genre_template, ask the must-ask questions, then start_game_project.')
        data = store.load()
        game = data['game']
        lines = [f"# {game.get('name', '?')} ({game.get('genre', '?')})", f"Pitch: {game.get('pitch', '')}",
                 f"Memory folder: {store.memory_dir()}", '']

        sections = store.ordered_sections(data['design'])
        missing = [store.section_title(k) for k in store.DESIGN_SECTIONS if k not in data['design']]
        lines.append(f"Design sections written: {', '.join(store.section_title(k) for k in sections) or 'none'}.")
        if missing:
            lines.append(f"Not written yet: {', '.join(missing)}.")

        decisions = store.active_decisions(data)
        by_user = sum(1 for d in decisions if d['source'] == 'user')
        lines.append(f"Decisions: {len(decisions)} active ({by_user} by the user, {len(decisions) - by_user} AI defaults).")

        tasks = data['tasks']
        if tasks:
            done = sum(1 for t in tasks if t['status'] == 'done')
            lines.append(f"Tasks: {done}/{len(tasks)} done.")
            doing = [t for t in tasks if t['status'] == 'doing']
            blocked = [t for t in tasks if t['status'] == 'blocked']
            todo = [t for t in tasks if t['status'] == 'todo']
            if doing:
                lines += ['In progress:'] + [_task_line(t) for t in doing]
            if blocked:
                lines += ['Blocked:'] + [_task_line(t) for t in blocked]
            if todo:
                current = todo[0]['milestone']
                lines += [f'Next up ({current}):'] + [_task_line(t) for t in todo if t['milestone'] == current][:6]
        else:
            lines.append('Tasks: none yet (seed_plan_from_template).')

        assets = data['assets']
        if assets:
            counts = {s: sum(1 for a in assets if a['status'] == s) for s in store.ASSET_STATUSES}
            lines.append(f"Assets: {counts['provided']} provided, {counts['placeholder']} using placeholders, {counts['needed']} still needed.")
        packs = data.get('packs', [])
        if packs:
            missing = [k for k in packs if k['status'] == 'suggested' and k['priority'] == 'essential']
            added = sum(1 for k in packs if k['status'] == 'added')
            lines.append(f"Asset packs: {added} added, {sum(1 for k in packs if k['status'] == 'suggested')} still suggested.")
            if missing:
                lines.append('Essential packs not added yet (remind the user): ' + ', '.join(f"{k['id']} {k['name']}" for k in missing))
        return '\n'.join(lines)

    # ------------------------------------------------------------------ templates & project

    @toolset_registry.tool_call
    @staticmethod
    def get_genre_template(genre: str) -> str:
        """Returns the knowledge for a genre: must-ask questions (with options and a recommendation), questions to ask
        when relevant, AI defaults with rationale, a milestone plan and typical asset needs. Unknown genres return the
        generic template; the AI then applies conventions of the genres the user names.

        Example: get_genre_template("survival")

        Args:
            genre: Genre name, e.g. "survival".

        Returns:
            The template as JSON, plus the list of genres with dedicated templates.
        """
        template = templates.load(genre)
        header = (f"Template '{template['genre']}' (genres with dedicated templates: {', '.join(templates.available())}).\n")
        return header + json.dumps(template, indent=2, ensure_ascii=False)

    @toolset_registry.tool_call
    @staticmethod
    def start_game_project(name: str, genre: str, pitch: str, overwrite: bool = False) -> str:
        """Creates the game project memory after the first interview answers. Refuses if a project already exists unless
        overwrite is true (ask the user first).

        Example: start_game_project("Embers of the Wild", "survival", "Survive a fantasy wilderness...")

        Args:
            name: Working title of the game.
            genre: Main genre (e.g. "survival"); mixes are fine, e.g. "survival RPG".
            pitch: One or two sentence pitch in the user's words.
            overwrite: Replace an existing project memory.

        Returns:
            Where the memory is stored.
        """
        if store.exists() and not overwrite:
            raise ValueError('A game project already exists here. Use get_project_memory, or pass overwrite=true '
                             'only if the user wants to start over.')
        data = store._empty()
        data['game'] = {'name': name.strip(), 'genre': genre.strip(), 'pitch': pitch.strip(),
                        'template': templates.resolve(genre), 'created': store._now()}
        store.save(data)
        return (f"Created game project '{name}' ({genre}) in {store.memory_dir()}. Next: write design sections, record "
                f"decisions (user answers and AI defaults), seed_plan_from_template, and seed the asset wishlist.")

    @toolset_registry.tool_call
    @staticmethod
    def seed_plan_from_template(genre: str | None = None) -> str:
        """Adds the genre template's milestones/tasks to the task board and its typical assets to the wishlist
        (skipping ones already there). Adjust afterwards to the user's scope.

        Example: seed_plan_from_template()

        Args:
            genre: Template to use; defaults to the project's genre.

        Returns:
            What was added.
        """
        data = _require_project()
        template = templates.load(genre or data['game'].get('genre', ''))
        existing_tasks = {(t['milestone'], t['title']) for t in data['tasks']}
        added_tasks = 0
        for m in template.get('milestones', []):
            for title in m['tasks']:
                if (m['milestone'], title) in existing_tasks:
                    continue
                data['tasks'].append({'id': store.next_id(data['tasks'], 'T'), 'milestone': m['milestone'],
                                      'phase': m.get('phase', ''), 'title': title, 'status': 'todo', 'notes': '',
                                      'updated': store._now()})
                added_tasks += 1
        existing_assets = {a['name'].lower() for a in data['assets']}
        added_assets = 0
        for a in template.get('asset_needs', []):
            if a['name'].lower() in existing_assets:
                continue
            data['assets'].append({'id': store.next_id(data['assets'], 'A'), 'name': a['name'], 'category': a['category'],
                                   'purpose': a.get('purpose', ''), 'status': 'needed',
                                   'placeholder': a.get('placeholder', ''), 'path': '', 'notes': ''})
            added_assets += 1
        added_packs = _seed_packs(data, template)
        store.save(data)
        return (f"Added {added_tasks} task(s), {added_assets} asset need(s) and {added_packs} asset pack suggestion(s) from the "
                f"'{template['genre']}' template. Fill in the pack search terms for the setting and art style, then show the user "
                f"get_asset_pack_list.")

    # ------------------------------------------------------------------ design document

    @toolset_registry.tool_call
    @staticmethod
    def write_design_section(section: str, content: str) -> str:
        """Writes (replaces) one section of the design document, in markdown. Recommended sections: overview, setting,
        player_experience, systems, world, progression, multiplayer, art_audio, ui_controls, platforms, scope.
        Mark anything the AI filled in as "(AI default)" so the user can spot it.

        Example: write_design_section("setting", "A cold fantasy wilderness...")

        Args:
            section: Section key (one of the recommended ones, or a new snake_case key).
            content: Markdown content for the section.

        Returns:
            Confirmation.
        """
        data = _require_project()
        key = section.strip().lower().replace(' ', '_')
        data['design'][key] = content
        store.save(data)
        return f"Wrote section '{store.section_title(key)}' ({len(content)} characters). GameDesign.md updated."

    @toolset_registry.tool_call
    @staticmethod
    def read_design_doc(section: str | None = None) -> str:
        """Reads the design document: one section, or the whole readable GameDesign.md (design, decisions, tasks, assets).

        Example: read_design_doc("systems")

        Args:
            section: Section key, or omit for the whole document.

        Returns:
            Markdown.
        """
        data = _require_project()
        if section:
            key = section.strip().lower().replace(' ', '_')
            if key not in data['design']:
                raise ValueError(f"No section '{key}'. Written: {', '.join(data['design']) or 'none'}.")
            return f"## {store.section_title(key)}\n\n{data['design'][key]}"
        return store.render_markdown(data)

    # ------------------------------------------------------------------ decisions

    @toolset_registry.tool_call
    @staticmethod
    def record_decision(topic: str, choice: str, source: str = 'user', rationale: str | None = None,
                        affects: list[str] | None = None) -> str:
        """Records a design decision. Use source "user" for the user's answers and "ai_default" for things the AI decided
        on its own (generic or simple choices). A new decision on the same topic supersedes the old one, so call this
        again when the user changes their mind, then update affected systems.

        Example: record_decision("Day/night cycle", "45 minutes per day", "user", "Shorter sessions")

        Args:
            topic: What the decision is about (short, reused for later changes).
            choice: The decision.
            source: "user" or "ai_default".
            rationale: Why.
            affects: Systems or tasks affected, e.g. ["survival stats", "T-012"].

        Returns:
            The decision id, and any decision it replaced.
        """
        if source not in store.DECISION_SOURCES:
            raise ValueError(f'source must be one of {store.DECISION_SOURCES}.')
        data = _require_project()
        new_id = store.next_id(data['decisions'], 'D')
        replaced = []
        for d in store.active_decisions(data):
            if d['topic'].strip().lower() == topic.strip().lower():
                d['supersededBy'] = new_id
                replaced.append(f"{d['id']} ({d['choice']})")
        data['decisions'].append({'id': new_id, 'topic': topic.strip(), 'choice': choice.strip(), 'source': source,
                                  'rationale': (rationale or '').strip(), 'affects': [str(a) for a in (affects or [])],
                                  'date': store._now(), 'supersededBy': ''})
        store.save(data)
        msg = f"Recorded {new_id}: {topic} = {choice} ({'user' if source == 'user' else 'AI default'})."
        if replaced:
            msg += f" Replaces {', '.join(replaced)}; update the systems that depend on it."
        return msg

    @toolset_registry.tool_call
    @staticmethod
    def list_decisions(source: str | None = None) -> str:
        """Lists active decisions (latest per topic), optionally only "user" or "ai_default" ones.
        Show the AI defaults to the user so they can change any of them.

        Example: list_decisions("ai_default")

        Args:
            source: Filter: "user" or "ai_default".

        Returns:
            One line per decision.
        """
        data = _require_project()
        rows = [d for d in store.active_decisions(data) if not source or d['source'] == source]
        if not rows:
            return 'No decisions recorded.'
        return '\n'.join(f"{d['id']} {d['topic']}: {d['choice']} [{'user' if d['source'] == 'user' else 'AI default'}]"
                         + (f" — {d['rationale']}" if d.get('rationale') else '') for d in rows)

    # ------------------------------------------------------------------ tasks

    @toolset_registry.tool_call
    @staticmethod
    def add_tasks(milestone: str, titles: list[str], phase: str | None = None) -> str:
        """Adds tasks to a milestone on the task board.

        Example: add_tasks("M2 Items & inventory", ["Add torch item", "Add berry item"])

        Args:
            milestone: Milestone name (new or existing).
            titles: Task titles.
            phase: Roadmap phase number, if any.

        Returns:
            The new task ids.
        """
        data = _require_project()
        ids = []
        for title in titles:
            task_id = store.next_id(data['tasks'], 'T')
            data['tasks'].append({'id': task_id, 'milestone': milestone.strip(), 'phase': phase or '', 'title': title.strip(),
                                  'status': 'todo', 'notes': '', 'updated': store._now()})
            ids.append(task_id)
        store.save(data)
        return f"Added {', '.join(ids)} to {milestone}."

    @toolset_registry.tool_call
    @staticmethod
    def update_task(task_id: str, status: str, notes: str | None = None) -> str:
        """Updates a task's status: todo, doing, done or blocked (say why in notes).

        Example: update_task("T-004", "done", "Built 4 km island")

        Args:
            task_id: Task id, e.g. "T-004".
            status: todo, doing, done or blocked.
            notes: Short note (what was done, or why it is blocked).

        Returns:
            Confirmation and milestone progress.
        """
        if status not in store.TASK_STATUSES:
            raise ValueError(f'status must be one of {store.TASK_STATUSES}.')
        data = _require_project()
        task = _find(data['tasks'], task_id, 'task')
        task['status'] = status
        if notes is not None:
            task['notes'] = notes.strip()
        task['updated'] = store._now()
        store.save(data)
        same = [t for t in data['tasks'] if t['milestone'] == task['milestone']]
        done = sum(1 for t in same if t['status'] == 'done')
        return f"{task['id']} '{task['title']}' is now {status}. {task['milestone']}: {done}/{len(same)} done."

    @toolset_registry.tool_call
    @staticmethod
    def list_tasks(status: str | None = None, milestone: str | None = None) -> str:
        """Lists tasks, optionally filtered by status and/or milestone (substring match).

        Example: list_tasks("todo")

        Args:
            status: todo, doing, done or blocked.
            milestone: Part of a milestone name.

        Returns:
            Tasks grouped by milestone.
        """
        data = _require_project()
        rows = [t for t in data['tasks']
                if (not status or t['status'] == status) and (not milestone or milestone.lower() in t['milestone'].lower())]
        if not rows:
            return 'No matching tasks.'
        lines, current = [], None
        for t in rows:
            if t['milestone'] != current:
                current = t['milestone']
                lines.append(f'{current}:')
            lines.append(_task_line(t))
        return '\n'.join(lines)

    # ------------------------------------------------------------------ assets

    @toolset_registry.tool_call
    @staticmethod
    def add_asset_need(name: str, category: str, purpose: str, placeholder: str | None = None) -> str:
        """Adds an asset the game needs to the wishlist (the user provides real assets; the AI uses a placeholder until then).

        Example: add_asset_need("Wolf", "Character", "Aggressive night predator", "Engine mannequin")

        Args:
            name: What is needed.
            category: Character, Foliage, Environment, Props, Building, Materials, UI, Audio, VFX...
            purpose: What it is for in the game.
            placeholder: What the AI uses until the real asset is provided.

        Returns:
            The asset id.
        """
        data = _require_project()
        asset_id = store.next_id(data['assets'], 'A')
        data['assets'].append({'id': asset_id, 'name': name.strip(), 'category': category.strip(), 'purpose': purpose.strip(),
                               'status': 'needed', 'placeholder': placeholder or '', 'path': '', 'notes': ''})
        store.save(data)
        return f"Added {asset_id} '{name}' to the asset wishlist."

    @toolset_registry.tool_call
    @staticmethod
    def update_asset(asset_id: str, status: str, path: str | None = None, notes: str | None = None) -> str:
        """Updates a wishlist asset: "placeholder" when the AI uses a stand-in, "provided" when the user's real asset is in
        the project (give its content path), or back to "needed".

        Example: update_asset("A-002", "provided", "/Game/Megaplant_Library/.../SM_Tree_Hornbeam_01_A")

        Args:
            asset_id: Asset id, e.g. "A-002".
            status: needed, placeholder or provided.
            path: Content path of the asset in use.
            notes: Licence, source or conversion notes.

        Returns:
            Confirmation.
        """
        if status not in store.ASSET_STATUSES:
            raise ValueError(f'status must be one of {store.ASSET_STATUSES}.')
        data = _require_project()
        asset = _find(data['assets'], asset_id, 'asset')
        asset['status'] = status
        if path is not None:
            asset['path'] = path.strip()
        if notes is not None:
            asset['notes'] = notes.strip()
        store.save(data)
        return f"{asset['id']} '{asset['name']}' is now {status}."

    @toolset_registry.tool_call
    @staticmethod
    def list_asset_wishlist(status: str | None = None) -> str:
        """Lists the asset wishlist, optionally only needed / placeholder / provided. Use it to tell the user which assets
        to add next.

        Example: list_asset_wishlist("needed")

        Args:
            status: needed, placeholder or provided.

        Returns:
            One line per asset.
        """
        data = _require_project()
        rows = [a for a in data['assets'] if not status or a['status'] == status]
        if not rows:
            return 'No matching assets.'
        return '\n'.join(f"{a['id']} [{a['status']}] {a['name']} ({a['category']}) — {a.get('purpose', '')}"
                         + (f" | using: {a['path'] or a['placeholder']}" if (a.get('path') or a.get('placeholder')) else '')
                         for a in rows)

    # ------------------------------------------------------------------ asset packs

    @toolset_registry.tool_call
    @staticmethod
    def get_asset_pack_list(status: str | None = None) -> str:
        """Returns the list of asset packs the user should add to the project (from Fab or the Marketplace), essential first,
        with what to search for, why it is needed and what to check before adding it. Show this to the user after the
        interview and whenever they ask what to get. The AI cannot download packs: the user adds them in the editor's Fab
        panel ("Add to Project"), then tells the AI, which sets them up and marks them added.

        Example: get_asset_pack_list("suggested")

        Args:
            status: suggested, added or skipped. Omit for all.

        Returns:
            A markdown checklist for the user.
        """
        data = _require_project()
        packs = [k for k in data.get('packs', []) if not status or k['status'] == status]
        if not packs:
            return 'No asset packs listed. Run seed_plan_from_template or add_asset_pack.'
        lines = ['# Asset packs for your game', '',
                 'Add these in the editor: open Fab, search for the pack, check the "must have" notes, then "Add to Project". '
                 'Tell me which ones you added and I will set them up. Free packs work fine; paid ones are your choice.', '']
        for priority in store.PACK_PRIORITIES:
            group = [k for k in packs if k['priority'] == priority]
            if not group:
                continue
            lines.append(f'## {priority.capitalize()}')
            for k in group:
                box = {'added': 'x', 'skipped': '-'}.get(k['status'], ' ')
                lines.append(f"- [{box}] **{k['name']}** ({k['id']}) — {k['why']}")
                lines.append(f"  - Search for: {k['search']}")
                if k.get('requirements'):
                    lines.append(f"  - Must have: {k['requirements']}")
                if k.get('path'):
                    lines.append(f"  - Added at: {k['path']}")
            lines.append('')
        return '\n'.join(lines)

    @toolset_registry.tool_call
    @staticmethod
    def add_asset_pack(name: str, category: str, search: str, why: str, priority: str = 'recommended',
                       requirements: str | None = None, covers: list[str] | None = None) -> str:
        """Adds an asset pack suggestion for the user (things the game needs that the template didn't list).

        Example: add_asset_pack("Medieval village kit", "Environment", "modular medieval village houses Nanite", "The starting village", "recommended")

        Args:
            name: What kind of pack.
            category: Animation, Props, Environment, Materials, Building, Character, UI, Audio, VFX...
            search: Search terms for Fab.
            why: What the game uses it for.
            priority: essential, recommended or optional.
            requirements: What to check before adding it (skeleton, style, poly count...).
            covers: Asset wishlist names it provides.

        Returns:
            The pack id.
        """
        if priority not in store.PACK_PRIORITIES:
            raise ValueError(f'priority must be one of {store.PACK_PRIORITIES}.')
        data = _require_project()
        packs = data.setdefault('packs', [])
        pack_id = store.next_id(packs, 'P')
        packs.append({'id': pack_id, 'name': name.strip(), 'category': category.strip(), 'priority': priority,
                      'search': search.strip(), 'why': why.strip(), 'requirements': (requirements or '').strip(),
                      'covers': [str(c) for c in (covers or [])], 'status': 'suggested', 'path': '', 'notes': ''})
        store.save(data)
        return f"Added asset pack suggestion {pack_id} '{name}' ({priority})."

    @toolset_registry.tool_call
    @staticmethod
    def update_asset_pack(pack_id: str, status: str, content_path: str | None = None, notes: str | None = None) -> str:
        """Marks an asset pack as added (give the content folder it landed in), skipped, or back to suggested.
        After a pack is added, set up its assets and update the asset wishlist entries it covers.

        Example: update_asset_pack("P-001", "added", "/Game/MeleeAnimations")

        Args:
            pack_id: Pack id, e.g. "P-001".
            status: suggested, added or skipped.
            content_path: Content folder of the added pack.
            notes: Pack name as added, licence notes...

        Returns:
            Confirmation and which wishlist entries it covers.
        """
        if status not in store.PACK_STATUSES:
            raise ValueError(f'status must be one of {store.PACK_STATUSES}.')
        data = _require_project()
        pack = _find(data.get('packs', []), pack_id, 'asset pack')
        pack['status'] = status
        if content_path is not None:
            pack['path'] = content_path.strip()
        if notes is not None:
            pack['notes'] = notes.strip()
        store.save(data)
        msg = f"{pack['id']} '{pack['name']}' is now {status}."
        if status == 'added' and pack.get('covers'):
            msg += ' Set up its assets and update these wishlist entries: ' + ', '.join(pack['covers']) + '.'
        return msg

