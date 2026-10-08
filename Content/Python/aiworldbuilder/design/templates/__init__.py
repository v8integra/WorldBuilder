"""Genre templates: what to ask, what to decide by default, milestone plans and typical asset needs."""

import json
import pathlib

_DIR = pathlib.Path(__file__).resolve().parent

# Synonyms the user or AI might use for a genre that has a template.
_ALIASES = {
    'survival': 'survival', 'survival game': 'survival', 'open world survival': 'survival', 'survival crafting': 'survival',
    'ark': 'survival', 'rust': 'survival', 'valheim': 'survival',
}


def available() -> list:
    return sorted(p.stem for p in _DIR.glob('*.json'))


def resolve(genre: str) -> str:
    key = (genre or '').strip().lower()
    if key in available():
        return key
    return _ALIASES.get(key, 'generic')


def load(genre: str) -> dict:
    name = resolve(genre)
    return json.loads((_DIR / f'{name}.json').read_text(encoding='utf-8'))
