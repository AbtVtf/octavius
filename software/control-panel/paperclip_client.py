"""
Paperclip API Client for Octavius

Bridges Octo's brain to Paperclip's task management system.
Octo can create projects, create tasks within projects, check status,
and get notified when work is done.
"""

import requests
import threading
import time

PAPERCLIP_URL = "http://127.0.0.1:3100/api"
PAPERCLIP_API_KEY = ""  # local_trusted mode — no auth needed
COMPANY_ID = "fbbaba35-ae9f-4fb6-89eb-6e4da69d23ff"

_headers = {
    "Content-Type": "application/json",
}
if PAPERCLIP_API_KEY:
    _headers["Authorization"] = f"Bearer {PAPERCLIP_API_KEY}"


# ---- Projects ----

def create_project(name, description=""):
    """Create a new project in Octo Corp. Returns project dict or None."""
    try:
        resp = requests.post(
            f"{PAPERCLIP_URL}/companies/{COMPANY_ID}/projects",
            headers=_headers,
            json={"name": name, "description": description},
            timeout=10,
        )
        if resp.status_code in (200, 201):
            p = resp.json()
            _project_cache[p["name"].lower()] = p["id"]
            return {"id": p["id"], "name": p["name"], "status": p["status"]}
        else:
            print(f"  [paperclip] create_project error {resp.status_code}: {resp.text[:200]}")
            return None
    except Exception as e:
        print(f"  [paperclip] create_project error: {e}")
        return None


def list_projects():
    """List all projects in Octo Corp."""
    try:
        resp = requests.get(
            f"{PAPERCLIP_URL}/companies/{COMPANY_ID}/projects",
            headers=_headers,
            timeout=10,
        )
        if resp.status_code == 200:
            projects = resp.json()
            return [
                {"id": p["id"], "name": p["name"], "status": p.get("status", "active")}
                for p in projects
                if not p.get("archivedAt")
            ]
        return []
    except Exception as e:
        print(f"  [paperclip] list_projects error: {e}")
        return []


_project_cache = {}  # name.lower() -> id — cleared on startup

def _find_project_id(project_name):
    """Find project ID by name (case-insensitive). Returns ID or None."""
    key = project_name.lower()
    if key in _project_cache:
        return _project_cache[key]
    projects = list_projects()
    for p in projects:
        _project_cache[p["name"].lower()] = p["id"]
    return _project_cache.get(key)


# ---- Tasks ----

ATLAS_AGENT_ID = "4f1214dd-908f-4070-b2c1-af2ad06c5293"  # Research Director
NOVA_AGENT_ID = "a859a777-371f-45c9-8c2c-dd9a7510f9b1"   # Research Engineer

def create_task(title, description, project_name=None, priority="medium"):
    """Create a new task assigned to Nova. Assigns to project if project_name is given."""
    payload = {
        "title": title,
        "description": description,
        "priority": priority,
        "assigneeAgentId": ATLAS_AGENT_ID,
    }

    if project_name:
        pid = _find_project_id(project_name)
        if pid:
            payload["projectId"] = pid
        else:
            # Auto-create the project
            new_proj = create_project(project_name)
            if new_proj:
                payload["projectId"] = new_proj["id"]
                print(f"  [paperclip] Auto-created project: {project_name}")

    try:
        resp = requests.post(
            f"{PAPERCLIP_URL}/companies/{COMPANY_ID}/issues",
            headers=_headers,
            json=payload,
            timeout=10,
        )
        if resp.status_code in (200, 201):
            task = resp.json()
            return {
                "id": task["id"],
                "identifier": task["identifier"],
                "title": task["title"],
                "status": task["status"],
                "project": project_name or "unassigned",
            }
        else:
            print(f"  [paperclip] create_task error {resp.status_code}: {resp.text[:200]}")
            return None
    except Exception as e:
        print(f"  [paperclip] create_task error: {e}")
        return None


def list_tasks(status=None, project_name=None):
    """List tasks. Optionally filter by status and/or project."""
    try:
        resp = requests.get(
            f"{PAPERCLIP_URL}/companies/{COMPANY_ID}/issues",
            headers=_headers,
            timeout=10,
        )
        if resp.status_code == 200:
            tasks = resp.json()
            if isinstance(tasks, dict) and "items" in tasks:
                tasks = tasks["items"]
            if status:
                tasks = [t for t in tasks if t.get("status") == status]
            if project_name:
                pid = _find_project_id(project_name)
                if pid:
                    tasks = [t for t in tasks if t.get("projectId") == pid]
            return [
                {
                    "id": t["id"],
                    "identifier": t["identifier"],
                    "title": t["title"],
                    "status": t["status"],
                    "priority": t.get("priority", "medium"),
                    "projectId": t.get("projectId"),
                }
                for t in tasks[:20]
            ]
        return []
    except Exception as e:
        print(f"  [paperclip] list_tasks error: {e}")
        return []


def get_task(task_id):
    """Get a single task by ID."""
    try:
        resp = requests.get(
            f"{PAPERCLIP_URL}/companies/{COMPANY_ID}/issues/{task_id}",
            headers=_headers,
            timeout=10,
        )
        if resp.status_code == 200:
            return resp.json()
        return None
    except Exception as e:
        print(f"  [paperclip] get_task error: {e}")
        return None


def comment_on_task(task_id, text):
    """Add a comment to a task."""
    try:
        resp = requests.post(
            f"{PAPERCLIP_URL}/issues/{task_id}/comments",
            headers=_headers,
            json={"body": text},
            timeout=10,
        )
        return resp.status_code in (200, 201)
    except Exception as e:
        print(f"  [paperclip] comment error: {e}")
        return False


def update_task_status(task_id, status):
    """Update task status (backlog, in_progress, done, cancelled)."""
    try:
        resp = requests.put(
            f"{PAPERCLIP_URL}/companies/{COMPANY_ID}/issues/{task_id}",
            headers=_headers,
            json={"status": status},
            timeout=10,
        )
        return resp.status_code == 200
    except Exception as e:
        print(f"  [paperclip] update_task error: {e}")
        return False


# ---- Event Poller ----

class PaperclipEventPoller:
    """Background thread that watches for task status changes."""

    def __init__(self, on_task_completed=None, poll_interval=30):
        self.on_task_completed = on_task_completed
        self.poll_interval = poll_interval
        self._known_completed = set()
        self._running = False

    def start(self):
        self._running = True
        for t in list_tasks(status="done"):
            self._known_completed.add(t["id"])
        t = threading.Thread(target=self._poll_loop, daemon=True)
        t.start()

    def stop(self):
        self._running = False

    def _poll_loop(self):
        while self._running:
            time.sleep(self.poll_interval)
            try:
                done_tasks = list_tasks(status="done")
                for task in done_tasks:
                    if task["id"] not in self._known_completed:
                        self._known_completed.add(task["id"])
                        if self.on_task_completed:
                            self.on_task_completed(task)
            except Exception as e:
                print(f"  [paperclip] poll error: {e}")
