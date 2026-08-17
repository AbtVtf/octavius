"""
Octo's Mind — Living documents that form his inner world.

Files:
  identity.md     — who he thinks he is
  relationships.md — people he knows
  journal.md      — daily entries
  opinions.md     — views he's formed
  dreams.md       — long-term aspirations
"""

import os
from datetime import datetime

MIND_DIR = os.path.dirname(__file__)

FILES = {
    "identity": "identity.md",
    "relationships": "relationships.md",
    "journal": "journal.md",
    "opinions": "opinions.md",
    "dreams": "dreams.md",
    "plan": "plan.md",
}


def read(name):
    """Read a mind file. Returns content string."""
    path = os.path.join(MIND_DIR, FILES.get(name, name))
    try:
        with open(path) as f:
            return f.read()
    except FileNotFoundError:
        return ""


def write(name, content):
    """Overwrite a mind file."""
    path = os.path.join(MIND_DIR, FILES.get(name, name))
    with open(path, "w") as f:
        f.write(content)


def append(name, text):
    """Append text to a mind file."""
    path = os.path.join(MIND_DIR, FILES.get(name, name))
    with open(path, "a") as f:
        f.write(text)


def read_all():
    """Read all mind files, return as a formatted string for prompt injection."""
    sections = []
    for name, filename in FILES.items():
        content = read(name).strip()
        if content:
            sections.append(content)
    return "\n\n".join(sections)


def journal_entry(text):
    """Add a dated journal entry."""
    today = datetime.now().strftime("%Y-%m-%d")
    journal = read("journal")

    # Check if today's date already has an entry
    if f"## {today}" in journal:
        # Append to today's entry
        append("journal", f"\n{text}\n")
    else:
        # New day
        append("journal", f"\n## {today}\n{text}\n")


def today():
    """Return current date and time as a string."""
    return datetime.now().strftime("%A, %B %d, %Y at %H:%M")
