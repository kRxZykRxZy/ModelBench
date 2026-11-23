# ModelBench TODO

Shared task board for the agent swarm. Agents append to their sections and re-read this file periodically to pick up new work.

## Coordination rules
- Append-only to your section; never delete another agent's entries.
- Mark tasks `[DONE]` when finished.
- Idea-finders append `[IDEA-xxx]` entries to the Ideas section.
- Bug-finder appends `[BUG-xxx]` entries to the Bugs section.
- Workers: if a new IDEA/BUG is in your domain, add a `[TASK-xxx]` for yourself and implement it.
- Re-read this file every ~10 tool calls.

---

## [IDEA] New feature ideas (idea-finders append here)
<!-- IDEA-FINDER-A and IDEA-FINDER-B append new ideas below -->
- [IDEA-101][suggested=agent-3] Voice input: push-to-talk mic button in ChatView using Qt Multimedia + OS speech-to-text, with live waveform and interim transcript before send
- [IDEA-102][suggested=agent-1] Plugin system: dynamically loaded plugins that register new tools, sidebar views, and settings pages through a stable versioned plugin API with capability-based permissions
- [IDEA-103][suggested=agent-3] Multi-model comparison: side-by-side chat panes running the same prompt against 2-3 local models with per-pane latency, tok/s, and memory stats
- [IDEA-104][suggested=agent-4] A/B testing harness: replay a saved prompt set against two model/settings configurations and emit a scored comparison report (quality, latency, tokens)
- [IDEA-105][suggested=agent-7] Prompt library: searchable, versioned prompt collection with {{variables}}, tags, and one-click insert into the chat composer
- [IDEA-106][suggested=agent-3] Rich chat rendering: markdown + syntax-highlighted code blocks with copy button and token-streamed re-render (ChatView currently shows plain text only)
- [IDEA-107][suggested=agent-3] Quantization picker in ModelsView: parse GGUF file lists into size/quality badges and recommend a quant based on HardwareDetector RAM/VRAM
- [IDEA-108][suggested=agent-6] Slash-command palette in the chat composer (/model, /save, /export, /mcp, /clear) with fuzzy-filtered popup and shortcuts
- [IDEA-109][suggested=agent-6] Customizable keyboard shortcuts with a Ctrl+K cheat-sheet overlay listing every binding and command
- [IDEA-110][suggested=agent-7] RAG over local docs: drag-drop a folder/PDF into chat, embed it into the SQLite vector store, and answer with inline source citations
- [IDEA-111][suggested=agent-3] Full theming system: light/dark/high-contrast + accent picker persisted to DB (Theme.h colors are hardcoded CONSTANT and SettingsView says "Light mode coming soon")
- [IDEA-112][suggested=agent-3] Conversation export: one-click export chat to Markdown/HTML including model, sampling settings, and timing metadata
- [IDEA-113][suggested=agent-6] Split-pane file viewer: open files referenced by agent tool calls in a side pane with syntax highlighting and diff-vs-HEAD toggle
- [IDEA-114][suggested=agent-1] Undoable agent edits: record every write_file/edit_file as a change set with a one-click "revert last agent edit" button in the timeline
- [IDEA-115][suggested=agent-2] Usage dashboard: per-model token/cost history charts, credit burn-rate projection, and low-balance forecast
- [IDEA-116][suggested=agent-3] Conversation organization: folders, tags, full-text search, rename, and pin-to-top in a sidebar recents tree (ConversationManager is currently a flat list)
- [IDEA-117][suggested=agent-6] Local API server console: start/stop the OpenAI-compatible endpoint from the UI with port picker, live request log, and copyable curl snippets (LocalApiServer.cpp is currently a stub)
- [IDEA-118][suggested=agent-4] API playground: fill-and-send request form against the local /v1 endpoint showing raw JSON, headers, and timing — doubles as living API docs
- [IDEA-119][suggested=agent-3] Chat controls: stop generation mid-stream, regenerate last reply, and edit-and-resend the last user message
- [IDEA-120][suggested=agent-1] Shareable run bundles: export a full agent run (timeline + tool args/outputs + settings) as a JSON/Markdown bundle for bug reports and repros
- [IDEA-121][suggested=agent-2] Inference performance: keep the KV cache warm across turns within a conversation (with sliding-window context trim) instead of clearing it before every generation
- [IDEA-122][suggested=agent-2] Reproducibility: persist a full run manifest per conversation (model, GGUF/llama.cpp version, chat template, seed, sampler params, context length) and support JSON export/import for bug reports and comparisons
- [IDEA-123][suggested=agent-4] Model benchmarking: built-in local eval harness running fixed prompt suites (reasoning, coding, knowledge) with token/latency stats and cross-model comparison reports
- [IDEA-124][suggested=agent-5] Performance regression CI: benchmark release candidates (tok/s, TTFT, peak RSS) against committed baselines and fail the build on regressions
- [IDEA-136][suggested=agent-3] Contextual empty states: every empty view gets a headline + one-click CTA (Home shows "No model loaded" with no action; ModelsView library is bare)
- [IDEA-137][suggested=agent-4] Offline docs viewer: bundle ARCHITECTURE/BUILD/tool-reference docs into the app with full-text search so help works with no network
- [IDEA-138][suggested=agent-7] Cross-conversation semantic search: query all past chats via the vector store and jump straight to the matching message
- [IDEA-139][suggested=agent-6] Snippet manager: save reusable prompt/code snippets with {{variables}}, insertable from the composer via Tab-completion
- [IDEA-140][suggested=agent-3] Themeable embedded terminal: ANSI color palette + font size that follows the app theme (dark/light) once agent terminal lands
- [IDEA-141][suggested=agent-2] BYOK cloud fallback: store an OpenRouter key in OS secret storage with a daily spend cap and auto-fallback when the local model OOMs or context overflows
- [IDEA-142][suggested=agent-7] Memory inspector: transparent UI listing everything the agent remembers (scratchpad + project context) with edit/forget controls for user trust
- [IDEA-143][suggested=agent-1] Path-scoped permissions: "always allow" currently matches tool name only — extend to glob-scoped grants (allow write only under ./src) plus a permission audit log
- [IDEA-144][suggested=agent-4] Golden-prompt regression suite: curated prompt set with expected traits run against each build to catch model/prompt quality regressions
- [IDEA-145][suggested=agent-6] Live preview pane: HTML/Markdown preview that auto-reloads whenever the agent edits the source file (builds on the file watcher)
- [IDEA-146][suggested=agent-2] Live spend meter: header chip showing tokens/credits burned during the current generation, with a per-message cost tooltip
- [IDEA-147][suggested=agent-6] Drag-and-drop attach: drop text, files, or URLs onto the composer to attach them as context (file → indexer, URL → fetcher, code → snippet)
- [IDEA-148][suggested=agent-7] Synced prompt/snippet library: back up the prompt library and snippets to the user's own Supabase account so they roam with the install
- [IDEA-149][suggested=agent-5] Portable mode: run entirely from one folder (data, models, DB beside the exe) for USB/locked-down machines with no AppData writes
- [IDEA-150][suggested=agent-3] Sticky response card: when user scrolls up mid-generation, pin a compact floating card with the streaming tail so they never lose the live output
- [IDEA-151][suggested=agent-6] Wire the new FileWatcher into a live HTML/Markdown preview pane (follows on IDEA-145) and auto-re-run CodeRunner snippets when their watched source file changes.
- [IDEA-152][suggested=agent-6] Extend TerminalView with 256-colour / true-colour SGR (38;5 and 38;2) and multiple tabs, each backed by its own TerminalSession.

## [BUG] Known bugs (bug-finder appends here)
<!-- BUG-FINDER appends confirmed bugs below -->
- [BUG-201][found=agent-1][fixed=agent-1] AgentMemory.h had Q_DECLARE_METATYPE(MemoryNote) inside namespace ModelBench — GCC error "specialization in different namespace". Moved to global scope as Q_DECLARE_METATYPE(ModelBench::MemoryNote). Agent-7: please keep it there going forward.
- [BUG-202][owner=agent-1] Build integration: root CMakeLists.txt missing app/agent and app/tools sources; duplicate ModelDownloads.cpp; tests/CMakeLists.txt missing add_subdirectory(tools/agent). See tests/bug_reports/audit_2026-11-23.md.
- [BUG-203][owner=agent-3] QML ModelsView.qml has duplicate `id: localList` (two ListViews) -> QML load failure.
- [BUG-204][owner=agent-3] QML child views reference undefined `window` id for navigation (SettingsView, OnboardingWizard, HomeView, CreditsView, ChatView) -> runtime ReferenceError.
- [BUG-205][owner=agent-3] QML HeaderBar.qml writes `Theme.mode` but Theme has no `mode` property/setter -> dead toggle + binding error.
- [BUG-206][owner=agent-3] QML HomeView quick action "Model Fit Calculator" navigates to non-existent "calculator" view -> no-op.
- [BUG-207][owner=agent-11] SecretStorage builds file paths from unsanitized service/account and createSecretStorage() leaks raw pointer.
- [BUG-208][owner=agent-4] MCPServerManager leaks MCPClient on reconnect/remove (new client retained, pointer dropped without deleteLater).
- [BUG-209][found=agent-6][open] New app/ui/components/TerminalView.qml and CodeRunnerView.qml are not listed in qt_add_qml_module's QML_FILES, so importing them as ModelBench components fails at runtime until the main agent adds them (see NOTE-127).

## [TODO] Assigned work

### [agent-1] Agent orchestrator & tools (model: hy4-flash-free)
- [DONE][TASK-001][owner=agent-1] Implement tool registry in app/tools/ToolRegistry.h/.cpp with registration, listing, and execution dispatch
- [DONE][TASK-002][owner=agent-1] Implement approval gate: read-only auto-approve, write/execute require confirmation with argument preview
- [DONE][TASK-003][owner=agent-1] Implement file system tools: read_file, write_file, edit_file, list_dir, search_code
- [DONE][TASK-004][owner=agent-1] Implement terminal tool: run_terminal with cwd, timeout, env, streaming output
- [DONE][TASK-005][owner=agent-1] Implement git tools: git_status, git_diff, git_commit, git_branch, git_log, git_push, git_pull
- [DONE][TASK-006][owner=agent-1] Implement agent orchestrator loop: plan -> tool call -> observe -> refine, with cancellation
- [DONE][TASK-121][owner=agent-1][from IDEA-114] Undoable agent edits: app/tools/FileEditHistory.h/.cpp, wired into write_file/edit_file via FileToolBase::setHistory, undoLastEdit() on the orchestrator
- [DONE][TASK-122][owner=agent-1][from IDEA-120] Shareable run bundles: AgentOrchestrator::exportRun() emits goal, steps, tool log, memory and edit journal as JSON
- [DONE][TASK-123][owner=agent-1][from IDEA-143] Path-scoped approval grants + audit log in ToolApproval (setScopedAllowance / isScopeAllowed / approvalLog)
- [NOTE-124][owner=agent-1] BUILD INTEGRATION NEEDED: root CMakeLists.txt MODELFIT_SOURCES/HEADERS must add app/tools/{ToolApproval,ToolRegistry,FileEditHistory,FileSystemTools,TerminalTool,GitTools}.cpp/.h and app/agent/{AgentPlanner,AgentOrchestrator}.cpp/.h; tests/CMakeLists.txt must add_subdirectory(tools) and add_subdirectory(agent) (fragment CMakeLists.txt files already provided in both dirs). I did not edit CMakeLists.txt due to strict file ownership.
- [NOTE-125][owner=agent-1] AgentMemory.h/.cpp were expanded by agent-7 (TASK-029); my orchestrator uses only the preserved compatibility API (addNote/notes/buildContextSummary/toJson). I left their version intact to avoid clobbering their work.
- [NOTE-126][owner=agent-1] Per main-agent instruction, subagents must not run builds/ctest. All new sources were syntax-checked with g++ -std=c++20 -Wall -Wextra -Wpedantic (clean); final build/test execution is for the main agent.

### [agent-2] Backend & credits (model: deepseek-v4.1-flash-free)
- [TASK-007][owner=agent-2][DONE] Implement Supabase schema (profiles, credits, entitlements) in server/src/lib/schema.sql
- [TASK-008][owner=agent-2][DONE] Implement credit ledger: grant on purchase, deduct on cloud AI usage
- [TASK-009][owner=agent-2][DONE] Implement OpenRouter proxy: POST /ai/chat with model allowlist, cost tracking, streaming
- [TASK-010][owner=agent-2][DONE] Implement Whop webhook handler for subscription lifecycle
- [TASK-011][owner=agent-2][DONE] Add mock mode for local dev (already partially done)

### [agent-3] UI/UX (model: nemotron-3.5-super-vl-free)
- [DONE][TASK-012][owner=agent-3] Build agent timeline UI in ChatView (step cards with tool name, args, output, approval)
- [DONE][TASK-013][owner=agent-3] Build git panel view with diff viewer, commit composer, branch switcher
- [DONE][TASK-014][owner=agent-3] Build credits UI: balance display, low-balance warning, upgrade prompt
- [DONE][TASK-015][owner=agent-3] Build onboarding wizard (hardware -> engine -> models -> account -> ready)
- [DONE][TASK-016][owner=agent-3] Fix QML anchors-on-layout warnings across all views (audited; codebase already uses Layout.* inside layouts; new code follows suit)
- [DONE][TASK-043][owner=agent-3] LoginView improvements: better error display, loading states
- [DONE][TASK-044][owner=agent-3] Add agent mode toggle in chat composer

### [agent-4] Integration & tests (model: beam-free)
- [TASK-017][owner=agent-4] Add integration tests for agent tool loop with mock MCP server [DONE]
- [TASK-018][owner=agent-4] Add integration tests for credits deduction flow [DONE]
- [TASK-019][owner=agent-4] Wire app services to QML context properties (verify all connections work) [DONE]
- [TASK-020][owner=agent-4] Update docs for agent mode, tools, git, credits [DONE]
- [TASK-043][owner=agent-4] Add tests for AccountManager login/signup flow (mock server) [DONE]

### [agent-5] Packaging & CI (model: beam-free)
- [TASK-021][owner=agent-5][DONE] Create Windows installer script (Qt deploy + llama libs + icon)
- [TASK-022][owner=agent-5][DONE] Create macOS .app/.dmg build script
- [TASK-023][owner=agent-5][DONE] Add auto-update mechanism (signed, verified, HTTPS)
- [TASK-024][owner=agent-5][DONE] Add GitHub Actions release workflow

### [agent-6] IDE & tooling (model: hy4-flash-free)
- [DONE][TASK-025][owner=agent-6] Embedded PTY terminal: added `ModelBench::TerminalSession` to app/tools/TerminalTool.h/.cpp (agent-1's non-interactive `run_terminal` Tool is preserved untouched). Windows ConPTY APIs are resolved dynamically from kernel32 (CreatePseudoConsole/ResizePseudoConsole + PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE); macOS/Linux use posix_openpt/grantpt/unlockpt + fork/execve with TIOCSWINSZ. Poll-based, 10ms, non-blocking output; dataReceived(QString), processFinished(int), errorOccurred(QString), start/sendInput/resize/interrupt/close slots. QML: app/ui/components/TerminalView.qml with ANSI SGR colours/bold, history (Up/Down), Ctrl+C, Ctrl+L, auto-resize via FontMetrics, Restart/Clear, scroll-to-bottom.
- [DONE][TASK-026][owner=agent-6] Code runner: app/tools/CodeRunner.h/.cpp compiles/runs C++ (g++/clang++, -std=c++17), Python (python3/python) and JS (node/nodejs) in a QTemporaryDir, uses QProcess argument lists (never a shell), streams output via `output(stream, chunk)`, reports `finished(QVariantMap)` with ok/stdout/stderr/compileOutput/exitCode/durationMs/timedOut/truncated/error, supports cancel() and per-run timeout. QML: app/ui/components/CodeRunnerView.qml with language picker, availability indicator, sample loader, source editor and live output pane.
- [DONE][TASK-027][owner=agent-6] File system watcher: app/tools/FileWatcher.h/.cpp. Watches files plus parent directories (handles atomic saves and delete/re-create), MD5 content hashing suppresses editor duplicate events, 150ms debounce coalescing, fileChanged/fileRemoved/fileAdded signals, watch/unwatch/clear/rescan/enabled/debounceMs API.
- [DONE][TASK-028][owner=agent-6] Clipboard + notifications: app/tools/SystemIntegration.h/.cpp. `Clipboard` wraps QGuiApplication::clipboard() with a text property and change signal; `Notifier` emits notificationRequested(title,message) for an in-app toast and also calls QSystemTrayIcon::showMessage when the platform supports native notifications (lazy tray icon, generated icon so macOS accepts it).
- [NOTE-127][owner=agent-6] BUILD INTEGRATION NEEDED (main agent): root CMakeLists.txt MODELFIT_SOURCES/HEADERS must add app/tools/{TerminalTool,CodeRunner,FileWatcher,SystemIntegration}.cpp/.h (TerminalTool is not yet listed; agent-1 NOTE-124 covers the other tool files). qt_add_qml_module QML_FILES must add app/ui/components/TerminalView.qml and app/ui/components/CodeRunnerView.qml. Application::registerQmlTypes should expose context properties: `terminalSession` (TerminalSession), `codeRunner` (CodeRunner), `fileWatcher` (FileWatcher), `clipboard` (Clipboard), `notifier` (Notifier). All QML components use `typeof` guards so they load harmlessly if a property is not wired yet.
- [NOTE-128][owner=agent-6] Per main-agent instruction, no builds/ctest were run. Files were reviewed by inspection only; final compile/test is the main agent's job. ConPTY is loaded via GetProcAddress so no extra libs are needed; POSIX pty path uses libc only (no -lutil).

### [agent-7] Memory & search (model: iceman-free)
- [TASK-029][owner=agent-7] Implement agent memory: per-conversation scratchpad + project context
- [TASK-030][owner=agent-7] Implement vector store in SQLite with embedding support
- [TASK-031][owner=agent-7] Implement memory search tool (semantic + keyword)
- [TASK-032][owner=agent-7] Implement codebase indexer for project-level context

### [agent-8] Bug finder (model: deepseek-v4.1-flash-free)
- [TASK-033][owner=agent-8] Audit all C++ code for memory leaks, use-after-free, race conditions
- [TASK-034][owner=agent-8] Audit all QML for undefined property access and scope issues
- [TASK-035][owner=agent-8] Audit secret handling: ensure no tokens in logs, SQLite, or crash reports
- [TASK-036][owner=agent-8] Audit MCP tool execution for injection and approval bypass

### [agent-11] Security reviewer (model: nemotron-3.5-lightning-free)
- [TASK-039][owner=agent-11] Review auth flow, token storage, and session management - See test_auth_flow.js
- [TASK-040][owner=agent-11] Review billing webhook signature verification - See test_billing_webhook.js
- [TASK-041][owner=agent-11] Review MCP tool approval system for bypass vulnerabilities - See test_mcp_approval_bypass.js
- [TASK-042][owner=agent-11] Review file system tool sandboxing - See test_filesystem_sandbox.js

### [agent-9] Idea finder A (model: mimo-v2.6-flash-free)
- [TASK-037][owner=agent-9] Brainstorm UX improvements and append [IDEA] entries

### [agent-10] Idea finder B (model: flint-40b-free)
- [TASK-038][owner=agent-10] Brainstorm developer-experience improvements and append [IDEA] entries

### [agent-11] Security reviewer (model: nemotron-3.5-lightning-free)
- [TASK-039][owner=agent-11] Review auth flow, token storage, and session management
- [TASK-040][owner=agent-11] Review billing webhook signature verification
- [TASK-041][owner=agent-11] Review MCP tool approval system for bypass vulnerabilities
- [TASK-042][owner=agent-11] Review file system tool sandboxing

## [DONE] Completed
- [TASK-000][owner=main] Initial scaffold, rebrand, backend skeleton, credit system, auth, git tools (partial)
- [TASK-021][owner=agent-5] Windows installer script
- [TASK-022][owner=agent-5] macOS .app/.dmg build script
- [TASK-023][owner=agent-5] Auto-update mechanism
- [TASK-024][owner=agent-5] GitHub Actions release workflow
- [TASK-029][owner=agent-7][DONE] Implemented AgentMemory: per-conversation SQLite scratchpad, project context entries, in-memory compat notes/facts, VectorStore-backed remember/recall.
- [TASK-030][owner=agent-7][DONE] Implemented VectorStore in SQLite with embedding BLOBs, cosine similarity semantic search, FTS5 keyword search with LIKE fallback.
- [TASK-031][owner=agent-7][DONE] Implemented memory search tool interface: AgentMemory::searchTool/searchToolSchema with semantic+keyword hybrid recall and scratchpad/project context in results.
- [TASK-032][owner=agent-7][DONE] Implemented CodeIndexer: project file tree, content chunking, VectorStore-backed hybrid code search, binary/size/dir filtering.
