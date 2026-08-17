# PaperclipAI: Comprehensive Research Document

## Executive Summary

Paperclip (paperclip.ing / github.com/paperclipai/paperclip) is an open-source, MIT-licensed, self-hosted orchestration platform for running teams of AI coding agents as if they were a company. It launched on March 4, 2026 and within three weeks had amassed over 35,000 GitHub stars, making it one of the fastest-rising AI repositories of Q1 2026.

The core idea is a "company OS" — you define an org chart (CEO agent, CTO agent, engineer agents, etc.), assign goals, and Paperclip handles ticketing, delegation, scheduling, budget enforcement, and traceability. Claude Code is one of its primary first-class supported runtimes, via a built-in `claude_local` adapter that spawns and persists Claude Code CLI processes, injecting context and skills on each heartbeat wake.

Paperclip has a documented REST API (prefixed `/api`), an event-driven plugin system, an HTTP adapter for remote webhooks, and an environment-variable-based auth model that agents use to authenticate and self-report work. This makes it directly integrable with external tooling.

---

## Table of Contents

1. [What Is Paperclip?](#1-what-is-paperclip)
2. [Core Concepts](#2-core-concepts)
3. [Architecture](#3-architecture)
4. [Claude Code Integration (claude_local adapter)](#4-claude-code-integration-claude_local-adapter)
5. [REST API](#5-rest-api)
6. [Agent Authentication](#6-agent-authentication)
7. [Plugin System](#7-plugin-system)
8. [HTTP Adapter (Remote Webhook Integration)](#8-http-adapter-remote-webhook-integration)
9. [Notable Plugins in the Ecosystem](#9-notable-plugins-in-the-ecosystem)
10. [Installation and Deployment](#10-installation-and-deployment)
11. [Best Practices and Gotchas](#11-best-practices-and-gotchas)
12. [Comparisons](#12-comparisons)
13. [Integration Strategy for This Project](#13-integration-strategy-for-this-project)
14. [Resources and Links](#14-resources-and-links)

---

## 1. What Is Paperclip?

Paperclip is a **Node.js server + React UI** that acts as a control plane for a fleet of AI agents. It does NOT run the agents itself — it orchestrates them. It provides:

- A ticketing/task system (called "issues") where work is defined
- An org chart where agents have roles and hierarchy
- A heartbeat scheduler that wakes agents on a cron or event basis
- Budget enforcement per agent (token spend caps)
- A plugin architecture for extending behavior
- A REST API so external systems can push/pull tasks

The tagline: *"Your agents don't need better prompts. They need an org chart."*

It is entirely self-hosted, MIT licensed, and free. No Paperclip account is required.

---

## 2. Core Concepts

### Issues (Tasks / Tickets)

The fundamental unit of work. An issue has:
- `title`, `description`, `status`, `priority`
- `assigneeAgentId` (which agent owns it)
- `projectId` (which project it belongs to)
- A comment thread for instructions and responses
- Full tracing of every tool call and decision

Issues are analogous to GitHub/Linear tickets. An agent "checks out" an issue to lock it (preventing double-work), does the work, then posts progress updates as comments.

### Agents

Each agent is a definition in Paperclip with:
- A role/title (CEO, CTO, Frontend Engineer, etc.)
- A system prompt / personality
- An adapter (what runtime to use — `claude_local`, `http`, etc.)
- A monthly token budget
- An assigned project and heartbeat schedule

### Heartbeats

Heartbeats are the scheduling mechanism. On each heartbeat wake, an agent:
1. Calls the Paperclip API to confirm identity
2. Looks for new task assignments and @-mentions
3. Claims (checks out) a task from its queue
4. Does the actual work
5. Saves important context to memory files
6. Posts progress updates back to the issue

Heartbeats can be time-based (cron, e.g., every 15 minutes) or event-based (triggered by a webhook or a new message arriving).

### Projects

Groups of related issues. Every issue belongs to a project, and every project traces back to the company mission — so agents always have context for *why* they are doing a task.

### Org Chart

A real hierarchy. A CEO agent can delegate to a CTO agent, who can delegate to engineer agents. Delegation creates new issues assigned down the chain.

### Budget

Each agent has a monthly token budget. At 80% utilization a soft warning fires. At 100% the agent auto-pauses and new tasks are blocked. The human operator (the "board") can override and resume at any time.

---

## 3. Architecture

```
┌─────────────────────────────────────────────┐
│              Paperclip Server               │
│                                             │
│  ┌─────────────┐   ┌──────────────────────┐ │
│  │  React UI   │   │   Express.js REST API │ │
│  │  (Vite)     │   │   /api/...            │ │
│  └─────────────┘   └──────────────────────┘ │
│                                             │
│  ┌─────────────────────────────────────────┐ │
│  │    PostgreSQL + Drizzle ORM             │ │
│  │    (embedded, auto-setup on first run)  │ │
│  └─────────────────────────────────────────┘ │
│                                             │
│  ┌─────────────────────────────────────────┐ │
│  │              Plugin Bus                 │ │
│  │   (namespaced event routing)            │ │
│  └─────────────────────────────────────────┘ │
└─────────────────────────────────────────────┘
          │                │
     ┌────┴──────┐   ┌─────┴──────────────────┐
     │Adapters   │   │External Systems         │
     │(local)    │   │(HTTP webhooks, plugins) │
     │           │   │                         │
     │claude_local│  │GitHub Issues, Slack,     │
     │codex      │   │Discord, Linear, etc.    │
     │gemini     │   └─────────────────────────┘
     │shell cmd  │
     └───────────┘
```

Paperclip is the **control plane**, not the execution plane. The work cycle:

1. Paperclip scheduler fires a heartbeat for an agent
2. The adapter spawns / resumes the agent process (e.g., `claude` CLI)
3. The adapter injects context (task, company goals, memory files, skills)
4. The agent does work, calling the Paperclip REST API to report status
5. The adapter captures stdout, parses usage/cost, extracts session state
6. Results are stored; next heartbeat picks up from there

---

## 4. Claude Code Integration (claude_local adapter)

This is the most relevant section for this project. Paperclip has a **built-in** `claude_local` adapter specifically for running Claude Code CLI sessions.

### How It Works

- The adapter runs `claude` as a subprocess on the same machine as the Paperclip server
- It persists the **Claude Code session ID** between heartbeats
- On the next wake, it resumes the existing conversation — the agent retains full context without re-stating everything
- Session resume is **cwd-aware**: if the working directory changed, a fresh session starts instead
- If resume fails (unknown session error), the adapter automatically retries with a fresh session

### Skills Injection

- The adapter creates a temporary directory with symlinks to Paperclip "skills" (markdown files with instructions / tool specs)
- This directory is passed to Claude Code via `--add-dir`
- Skills are discoverable to the agent without polluting its working directory

### Requirements

- `claude` CLI must be installed and in PATH
- `ANTHROPIC_API_KEY` must be set in the environment or agent config

### What the Agent Receives at Each Heartbeat

Environment variables injected into every heartbeat run:

| Variable | Description |
|---|---|
| `PAPERCLIP_AGENT_ID` | This agent's UUID |
| `PAPERCLIP_API_KEY` | Agent's long-lived API key |
| `PAPERCLIP_RUN_ID` | UUID for this specific run |
| `PAPERCLIP_COMPANY_ID` | Company UUID |
| `PAPERCLIP_API_URL` | URL of the Paperclip server (e.g., `http://localhost:3100`) |
| `PAPERCLIP_TASK_ID` | Current task UUID (if agent has a checked-out task) |
| `PAPERCLIP_WAKE_REASON` | Why the agent was woken (`heartbeat`, `task_assigned`, `comment`, `approval`) |

The agent uses these env vars to call back to the Paperclip API, check out tasks, post comments, and report completion.

---

## 5. REST API

Paperclip exposes a **RESTful JSON API** for all control plane operations.

- **Base URL**: `http://localhost:3100` (default self-hosted)
- **Prefix**: All endpoints under `/api`
- **Response format**: JSON; successful responses return the entity directly
- **Pagination**: List endpoints support standard pagination query params; issues sorted by priority, others by creation date
- **Rate limiting**: None enforced in local/self-hosted deployments

### Key Endpoints (confirmed from docs)

#### Issues (Tasks)

```
GET    /api/companies/{companyId}/issues                  # List all issues
GET    /api/companies/{companyId}/issues/{issueId}        # Get one issue (by UUID or identifier e.g. PAP-42)
POST   /api/companies/{companyId}/issues                  # Create an issue
PUT    /api/companies/{companyId}/issues/{issueId}        # Update an issue
POST   /api/issues/{issueId}/comments                     # Add a comment to an issue
```

#### Create Issue Payload (POST body)

```json
{
  "title": "string",
  "description": "string (markdown)",
  "status": "todo | in_progress | done | cancelled",
  "priority": "urgent | high | normal | low",
  "assigneeAgentId": "uuid",
  "projectId": "uuid"
}
```

#### Agents

```
GET    /api/companies/{companyId}/agents                  # List agents
GET    /api/companies/{companyId}/agents/{agentId}        # Get agent
```

#### Companies / Projects

```
GET    /api/companies                                     # List companies
GET    /api/companies/{companyId}/projects                # List projects
```

The API docs (OpenAPI format) are at `docs.paperclip.ing/api/overview` and also mirrored on Mintlify at `mintlify.com/paperclipai/paperclip/api/issues`.

---

## 6. Agent Authentication

Three authentication methods exist:

### 1. Agent API Keys (primary for programmatic use)

- Generated when an agent is created; displayed once — if lost, regenerate
- Long-lived; stored securely by the operator
- Used in `Authorization: Bearer <key>` header on all API requests
- Scoped to a specific agent

### 2. Agent Run JWTs

- Short-lived tokens auto-generated for each heartbeat execution
- Injected into the agent's environment as `PAPERCLIP_API_KEY` during the run
- Scoped to that specific run; expire after it ends
- Agents use this for API calls made during execution

### 3. Session Cookies

- For the human web UI only — not relevant for agent/API integration

### Known Issue

There is a known bug (GitHub issue #856) where `PAPERCLIP_API_KEY` can be unusable inside Bash subshells during heartbeats in the `claude_local` adapter. The workaround is to use the agent's long-lived API key rather than the run JWT when calling back from subprocesses.

---

## 7. Plugin System

Paperclip has a first-class plugin architecture defined in `doc/plugins/PLUGIN_SPEC.md`.

### Plugin Anatomy

A plugin is a Node.js package that exports a `setup(ctx)` function. The `ctx` object provides:
- **Event bus**: emit and listen to namespaced events
- **Jobs**: register recurring background jobs
- **Tools**: expose callable tools to agents and other plugins
- **RPC**: `getData(key, params)` and `performAction(key)` for UI bridge hooks

### Plugin Lifecycle

1. Paperclip worker calls the plugin's `setup(ctx)` — plugin registers handlers
2. Host mounts plugin UI components and passes a bridge object
3. On shutdown, host sends a shutdown request triggering the plugin's `onShutdown()` hook

### Event Bus

Events are namespaced per plugin:
```
plugin.<plugin-name>.<event-name>
```

Example (ACP plugin):
```
plugin.paperclip-plugin-telegram.acp-spawn
plugin.paperclip-plugin-slack.acp-message
plugin.paperclip-plugin-discord.acp-close
```

### SDK

The plugin SDK is available as:
```
npm install @paperclipai/plugin-sdk
```

---

## 8. HTTP Adapter (Remote Webhook Integration)

For integrating Paperclip with remote systems (not local processes), the **HTTP adapter** sends a POST request to a configured webhook URL on each heartbeat.

### Configuration

```json
{
  "adapter": "http",
  "url": "https://your-server.com/paperclip-webhook",
  "method": "POST",
  "headers": {
    "Authorization": "Bearer ${secrets.MY_TOKEN}",
    "Content-Type": "application/json"
  },
  "timeout": 30000,
  "payloadTemplate": {
    "runId": "{{runId}}",
    "agentId": "{{agentId}}",
    "companyId": "{{companyId}}",
    "taskId": "{{taskId}}",
    "issueId": "{{issueId}}",
    "wakeReason": "{{wakeReason}}",
    "wakeCommentId": "{{wakeCommentId}}",
    "approvalId": "{{approvalId}}",
    "approvalStatus": "{{approvalStatus}}"
  }
}
```

Secrets are stored in Paperclip's secret vault and referenced via `${secrets.name}` syntax. Paperclip renders all `{{...}}` placeholders before sending.

### Execution Patterns

- **Synchronous**: webhook handler does the work and returns a result immediately
- **Asynchronous**: webhook fires a background job; the agent polls or receives a callback

### Task Checkout (concurrency safety)

The checkout mechanism is atomic — two agents cannot pick up the same task. Budget enforcement and checkout are transactional, preventing double-work and runaway spend.

---

## 9. Notable Plugins in the Ecosystem

The community curates plugins at `github.com/gsxdsm/awesome-paperclip`. Notable entries:

### Communication

| Plugin | Description |
|---|---|
| `paperclip-plugin-discord` | Bidirectional Discord integration: notifications, slash commands, community intelligence |
| `paperclip-plugin-slack` | Posts to Slack when issues are created, completed, or need approval |
| `paperclip-plugin-telegram` | Posts to Telegram for the same events |

### Development / Task Management

| Plugin | Description |
|---|---|
| `paperclip-plugin-github-issues` | Bidirectional GitHub Issues sync |
| `paperclip-plugin-writbase` | Bidirectional sync with WritBase tasks via webhooks |
| `paperclip-plugin-pipeline-controller` | Visual pipeline editor, auto-advance between agents, stuck detection, webhook notifications |

### Agent Runtimes

| Plugin / Adapter | Description |
|---|---|
| `paperclip-plugin-acp` | ACP (Agent Client Protocol) runtime — runs Claude Code, Codex, Gemini CLI from any chat platform (Telegram, Slack, Discord) |
| `hermes-paperclip-adapter` (NousResearch) | Runs Hermes Agent as a Paperclip employee |

### Utilities

| Plugin | Description |
|---|---|
| `paperclip-aperture` | Alternative "Focus" view that ranks approvals, issue activity, and human-facing events into now/next/ambient |
| `paperclip-plugin-company-wizard` | Bootstrap AI agent companies from modular templates |

### ACP Plugin Deep Dive

The `paperclip-plugin-acp` is particularly relevant — it is the bridge between chat platforms and the Claude Code subprocess:

- Chat plugins (Telegram/Slack/Discord) emit events on the event bus
- The ACP plugin listens and spawns coding agents as subprocesses over stdio
- Sessions persist across follow-up prompts within the same thread
- Supports "oneshot mode" — single-task sessions that auto-close after completion
- Follows the Agent Client Protocol (ACP) standard from Zed Industries

---

## 10. Installation and Deployment

### Quickest Start (no Node.js install needed)

```bash
npx paperclipai onboard
```

This walks through setup, configures the environment, and gets Paperclip running. The API starts at `http://localhost:3100`. An embedded PostgreSQL database is created automatically — no separate DB setup required.

### NPM Global Install

```bash
npm install -g paperclipai
paperclipai run
```

### Docker

A Dockerfile exists in the repo. Community image available at `hub.docker.com/r/reeoss/paperclipai-paperclip`. One-click VPS deploy also available on Hostinger.

A deploy template exists on Zeabur at `zeabur.com/templates/E6H44N`.

### Production

For production, point Paperclip at an external PostgreSQL database and deploy however you like. The `SKILL.md` (a Claude Code skill file included with Paperclip) helps agents assist with cloud deployment.

### Version

As of April 2026, the npm package is `paperclipai` version `2026.325.0`.

---

## 11. Best Practices and Gotchas

- **Session persistence**: The `claude_local` adapter persists session IDs. Do not delete or rotate the session store between runs unless you intend to start fresh.
- **PAPERCLIP_API_KEY in subshells**: Known bug (#856) — the API key env var may be inaccessible inside Bash subprocesses during heartbeats. Use the long-lived agent key instead of the run JWT when calling back from scripts.
- **Budget enforcement**: Set conservative monthly budgets when starting out. Agents that enter "Agentic Panic" (issue #447) can spiral into infinite loops, burning tokens trying to bypass execution approvals — set low limits initially.
- **Atomic checkout**: Always use the checkout API — don't assume a task is available just because it shows as `todo`. Two agents can race to claim tasks.
- **Skills injection path**: Skills are symlinked into a temp dir and passed via `--add-dir`. Do not hardcode paths to skill files in agent prompts.
- **Fresh install issue**: There is a known bug (#1569) where the initial install may not run the setup correctly. If the server fails to start, run `npx paperclipai onboard` again.
- **Cursor/Gemini agents**: Besides Claude Code, the `claude_local`-style local adapter pattern also works for Codex, Gemini CLI, Cursor, and `pi`.

---

## 12. Comparisons

| Feature | Paperclip | CrewAI | AutoGen | OpenDevin |
|---|---|---|---|---|
| License | MIT (open source) | MIT | MIT | Apache 2.0 |
| Self-hosted | Yes | Yes | Yes | Yes |
| UI / Dashboard | Yes (React) | No | No | Partial |
| Ticketing system | Yes (built-in) | No | No | No |
| Budget enforcement | Yes (per-agent) | No | No | No |
| Claude Code native | Yes (claude_local adapter) | No | No | No |
| Org chart / hierarchy | Yes | Yes (roles) | Yes (roles) | No |
| Plugin system | Yes (npm packages) | No | No | No |
| REST API | Yes | No | No | No |
| Heartbeat scheduler | Yes | No | No | No |

Paperclip occupies a unique niche: it is a **company simulation layer** on top of agent runtimes, rather than a framework for building agents. You bring your own runtime (Claude Code, Codex, etc.); Paperclip handles coordination, governance, and audit trails.

---

## 13. Integration Strategy for This Project

Given what we know, here are the realistic integration paths for the Octavius/Watcher project:

### Option A — Paperclip as Task Manager, Claude Code as Worker

Run a local Paperclip server. Define one or more agents using the `claude_local` adapter. Push issues to Paperclip via its REST API (from a script, CI, or manually). Paperclip wakes Claude Code on a heartbeat schedule, injects task context, and Claude Code does the work in the firmware/software directories.

**Pros**: Full audit trail, budget control, session persistence across heartbeats, org chart for multi-agent flows.
**Cons**: Requires a Node.js server running continuously; adds infrastructure overhead for a small team.

### Option B — HTTP Adapter Webhook

Run Paperclip externally and configure agents with the HTTP adapter. Point the webhook at a local script or server in this repo. Paperclip fires the webhook when a task needs work; the local handler spawns a Claude Code subprocess with the task context.

**Pros**: Paperclip handles scheduling and UI; execution stays local.
**Cons**: Requires a reachable webhook URL (either via tunnel or local network).

### Option C — Direct API Integration (No Local Paperclip)

Skip running Paperclip locally. Instead, use the Paperclip REST API to push issues into a shared Paperclip instance (if one is available/hosted). This is purely API-driven — create issues, poll for completions, read comments.

**Pros**: No server to manage.
**Cons**: Requires a hosted Paperclip instance; less control.

### Option D — Build a Paperclip Plugin

Write a custom plugin (using `@paperclipai/plugin-sdk`) that integrates Paperclip with this project's specific workflow — e.g., syncing Paperclip issues with the project's own task list, or triggering firmware builds when engineering tasks complete.

**Pros**: Deep, native integration with Paperclip's event bus and UI.
**Cons**: Most engineering effort; requires understanding the plugin spec.

### Recommended Starting Point

**Option A** is the most straightforward path to evaluate Paperclip:

```bash
npx paperclipai onboard
# follow prompts
# create an agent with claude_local adapter
# create an issue via the UI or REST API
# watch Paperclip invoke Claude Code on the next heartbeat
```

The REST API at `http://localhost:3100/api` can then be called from `assistant.py` or any other script to push new tasks programmatically.

---

## 14. Resources and Links

### Official

- **Homepage**: https://paperclip.ing/
- **GitHub repo**: https://github.com/paperclipai/paperclip
- **Documentation**: https://docs.paperclip.ing/
- **API docs (Mintlify)**: https://www.mintlify.com/paperclipai/paperclip/api/issues
- **HTTP adapter docs**: https://www.mintlify.com/paperclipai/paperclip/agents/http-adapter
- **Claude local adapter docs**: https://docs.paperclip.ing/adapters/claude-local
- **GitHub: docs/adapters/claude-local.md**: https://github.com/paperclipai/paperclip/blob/master/docs/adapters/claude-local.md
- **Plugin spec**: https://github.com/paperclipai/paperclip/blob/master/doc/plugins/PLUGIN_SPEC.md
- **AGENTS.md**: https://github.com/paperclipai/paperclip/blob/master/AGENTS.md
- **npm package**: https://www.npmjs.com/package/paperclipai
- **Docker image (community)**: https://hub.docker.com/r/reeoss/paperclipai-paperclip
- **Releases**: https://github.com/paperclipai/paperclip/releases

### Ecosystem

- **Awesome Paperclip (plugin list)**: https://github.com/gsxdsm/awesome-paperclip
- **ACP runtime plugin**: https://github.com/mvanhorn/paperclip-plugin-acp
- **Pipeline controller plugin**: https://github.com/labatt/paperclip-plugin-pipeline-controller
- **GitHub Issues sync plugin**: search `paperclip-plugin-github-issues` on GitHub
- **Hermes adapter (NousResearch)**: https://github.com/NousResearch/hermes-paperclip-adapter
- **Paperclip on DeepWiki**: https://deepwiki.com/paperclipai/paperclip/9.1-plugin-architecture-and-runtime

### Tutorials & Write-ups

- **MindStudio — Build a Multi-Agent Company with Paperclip and Claude Code**: https://www.mindstudio.ai/blog/how-to-build-multi-agent-company-paperclip-claude-code
- **Towards AI — The Open-Source OS for Zero-Human Companies**: https://pub.towardsai.net/paperclip-the-open-source-operating-system-for-zero-human-companies-2c16f3f22182
- **Geeky Gadgets — Turn Claude Code Into an AI Workforce**: https://www.geeky-gadgets.com/paperclip-claude-ai-business/
- **Zeabur deploy guide**: https://zeabur.com/blogs/deploy-paperclip-ai-agent-orchestration
- **Apidog overview**: https://apidog.com/blog/paperclip-ai-agent-company/
- **Substack "Company OS" breakdown**: https://nervegna.substack.com/p/paperclip-the-company-os-your-agents
- **Stanza — Paperclip Adapters course**: https://www.stanza.dev/courses/paperclip-adapters/

---

*Research compiled: April 4, 2026. Paperclip launched March 4, 2026. Information reflects the platform at approximately v2026.325.0.*
