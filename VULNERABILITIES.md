# VULNERABILITIES.md — ModelBench

**Repository:** `kRxZykRxZy/ModelBench`
**Branch audited:** `main`
**Commit audited:** `be192b4` — "Replace Navigation QML singleton with C++ context property so Navigation.go/currentView work reliably"
**Audit date:** 26 November 2026
**Audit method:** Static, read-only source audit. No code was executed, no exploits were run, no live requests were sent, no files in the repository were modified by the audit itself, and nothing was pushed during the audit.
**Scope:** The whole repository as cloned at that commit — Qt/C++ desktop app under `app/`, Node server under `server/`, build/CI/scripts, QML UI, docs, and tests. The `third_party/llama.cpp/` submodule was empty in the clone, so vendored llama.cpp / ggml code was **not** audited.
**Status of this file:** Audit findings only. Nothing in this file has been fixed by writing it. Every finding below needs a code change, a configuration change, or a documented decision to accept the risk.

> **A note on the filename.** The user asked for a file called `VULNERABILITYIES.MD`. This file is the canonical comprehensive document, spelled `VULNERABILITIES.md`. A stub file named `VULNERABILITYIES.MD` also exists at the repository root pointing here, so the requested spelling exists too.

---

## Contents

1. [Executive summary](#1-executive-summary)
2. [How to read this file](#2-how-to-read-this-file)
3. [Severity summary](#3-severity-summary)
4. [Wiring caveat — what is latent and what is reachable today](#4-wiring-caveat--what-is-latent-and-what-is-reachable-today)
5. [Top 10 most urgent findings](#5-top-10-most-urgent-findings)
6. [Findings — Critical](#6-findings--critical)
7. [Findings — High](#7-findings--high)
8. [Findings — Medium](#8-findings--medium)
9. [Findings — Low](#9-findings--low)
10. [Findings — Info](#10-findings--info)
11. [Cross-cutting themes](#11-cross-cutting-themes)
12. [Safe and mitigating patterns found](#12-safe-and-mitigating-patterns-found)
13. [Test gaps](#13-test-gaps)
14. [Prioritised remediation roadmap](#14-prioritised-remediation-roadmap)
15. [Responsible disclosure](#15-responsible-disclosure)
16. [Methodology, automated scans, and limitations](#16-methodology-automated-scans-and-limitations)
17. [Files reviewed](#17-files-reviewed)
18. [Appendix A — Source-ID to MB-ID mapping](#18-appendix-a--source-id-to-mb-id-mapping)
19. [Appendix B — Severity-count reconciliation](#19-appendix-b--severity-count-reconciliation)

---

## 1. Executive summary

ModelBench is a Qt/C++ desktop application with a Node (Express + Supabase + Whop) billing/AI backend, an agent that can run shells, code, git and MCP servers, a model downloader, an inference engine installer, and an auto-updater. At commit `be192b4` it is an early-stage codebase in which the dangerous capabilities were built **before** the controls that should bound them. That is the single most important conclusion of this audit.

**189 deduplicated findings: 10 Critical, 48 High, 73 Medium, 52 Low, 6 Info.**

The worst problems are not obscure bugs. They are the core design choices, and they compound:

- **The server fails open into mock mode.** If any Supabase variable is missing, the server silently becomes a development server with plaintext passwords, unsigned webhooks, and free paid checkouts — no startup failure, no warning, no `NODE_ENV` check (MB-001). In that mode anyone can grant themselves a paid tier and credits with no authentication, repeatedly (MB-002), and unsigned webhooks are accepted (MB-003). Even in live mode, billing entitlements can be read or created for any user without authentication (MB-011, MB-012), tiers are granted on substring matches of plan names (MB-015), and credit grants are not atomic (MB-017).
- **The desktop app has no sandbox at all.** The terminal tool, the code runner, MCP servers, and git all run as the app user with full filesystem and network access (MB-005). The terminal tool intentionally runs `shell=true` arbitrary shell commands (MB-004); the code runner executes arbitrary Python/JavaScript/C++ with the full parent environment (MB-006); MCP servers are arbitrary programs launched from a plaintext JSON config file (MB-007); git push/pull accept an arbitrary remote URL, including the classic `ext::` command-execution transport (MB-008).
- **The supply chain is unsigned end to end.** Release artefacts are published unsigned with no checksums, SBOM or provenance (MB-054). The llama.cpp engine binary is downloaded from a GitHub API response and extracted and loaded with no hash or signature check anywhere (MB-009). Model downloads have no integrity, size or resume verification, and cancelling a download reports it as finished (MB-048, MB-049). The auto-updater's Ed25519 public key is a truncated placeholder, so no update can ever install — and the pressure that creates to disable the check is itself the danger (MB-044). The update manifest itself is unsigned (MB-097).
- **Path containment is lexical, opt-in, and mostly not called.** `PathSecurity::isInside()` does not resolve symlinks, and the one place that most needs it — the model downloader — imports the header and never calls it (MB-010, MB-101). The model downloader is, as written, an unauthenticated arbitrary-file-write primitive (MB-010). Secret storage builds file paths by concatenating unsanitised `service`/`account` strings (MB-052). The filesystem tools' sandbox root defaults to empty, meaning no confinement at all (MB-030).
- **The approval gate, which is supposed to stand between a model and all of the above, trusts the tool being approved.** It defaults to fail-open `ReadOnly`, trusts each tool's self-reported risk level, can be silently disabled with `setApproval(nullptr)`, and can be bypassed session-wide with unauthenticated `AutoApprove` / `alwaysAllow` calls (MB-021–MB-025). It also emits raw tool arguments — including secrets — alongside its redacted preview (MB-025).
- **Secrets are handled carelessly in several places.** MCP bearer tokens are stored in plaintext `servers.json` (MB-027). The code indexer indexes secrets, stores them in plaintext SQLite, and hands them to the embedding provider (MB-053). The log redactor leaves `Bearer <token>` in the log file (MB-108). The database and settings store are plaintext with no file permissions (MB-105). Credentials are sent over plaintext HTTP when the user types an `http://` server URL (MB-047).

**What is *not* true, and should not be overstated:**

- Several of the most dangerous paths are **latent, not yet wired end to end** — see §4. Filesystem tools are registered only in tests, MCP tools are not wired into the agent, the agent's `run()` is not called from QML, and the local API server binds no socket at all. That lowers current exploitability. It does not make the code safe: the moment those paths are wired in, the Critical and High findings become directly reachable, including by a prompt-injected model.
- **No hardcoded secrets were found** by automated grep or by any of the four reviewers (see §16). The only "key" in the tree is the updater's placeholder public key, which is invalid by design.
- **No SQL injection was found** in the server (Supabase client, parameterised) or in the desktop app (bound `?` parameters everywhere). The injection-adjacent issues are FTS5 query syntax (MB-103) and an unescaped `ilike` email lookup (MB-068), not SQL injection.
- **No unsafe C functions** (`strcpy`, `strcat`, `sprintf`, `gets`, `system`, `popen`, `eval`) were found in the audited code.
- The updater, as written, **fails closed**: with the placeholder key, signature verification always fails and no update installs. That is the one place where fail-closed behaviour is correct — but it means the shipped updater does not work at all.

**The single highest-leverage fix is a sandbox** (MB-005). No other single change bounds as many findings. After that: fail the server closed on missing configuration (MB-001), fix the approval gate to fail closed (MB-021–MB-025), enforce path containment with one canonical helper used everywhere (MB-101), and verify hashes/signatures on the engine, models, and updates before anything is executed or loaded (MB-009, MB-010, MB-044, MB-048). Until those four are done, fixing individual Low findings is rearranging deck chairs.

---

## 2. How to read this file

**Finding IDs.** Every finding has a unique ID, `MB-001` … `MB-189`, assigned in severity order: Critical first (MB-001–MB-010), then High (MB-011–MB-058), Medium (MB-059–MB-131), Low (MB-132–MB-183), Info (MB-184–MB-189). Findings are presented grouped by area within each severity band, so IDs are not sequential inside a section. Appendix A maps every original reviewer ID (F-01, server C1, V-04, core H11, and so on) to its MB ID, so no finding from the four audit parts is lost.

**Severity meanings.**

| Severity | Meaning |
|---|---|
| Critical | Directly exploitable path to arbitrary code execution, account takeover, or free paid access, or a systemic fail-open that enables those. Fix before any production use. |
| High | Serious vulnerability or missing control that is exploitable with realistic attacker effort, or a design flaw that makes a Critical finding reachable. Fix before release. |
| Medium | Real vulnerability or missing control with constrained exploitability, a latent flaw that becomes High when wiring lands, or a defence-in-depth failure with meaningful impact. Fix in the next hardening pass. |
| Low | Defence-in-depth gap, correctness/robustness issue with security consequences, or information disclosure with limited impact. Fix opportunistically. |
| Info | Observation, not a vulnerability in itself — a stub, a weak primitive, or a future risk to track. |

**Status meanings — read these carefully, they are not interchangeable.**

| Status | Meaning |
|---|---|
| Confirmed | The vulnerable code (or its absence) was read in the source at the cited location. The defect is real. |
| Suspected | The code pattern is present, but whether it is exploitable depends on runtime behaviour, a caller, a library version, or a configuration that was not verified in this static audit. Do not treat as a proven exploit; do not dismiss it either. |
| Latent | The code is dangerous, but no current caller/wiring makes it reachable in this snapshot. It becomes exploitable when the corresponding feature is wired in. Latent findings must not be dropped on that basis — wiring them in without fixing them is the failure mode. |

Where reviewers disagreed on severity or status, the disagreement is recorded in the finding itself. The most notable: the GGUF model-load finding (MB-051) was rated Medium by one reviewer and High by another — this document rates it High, because the file is parsed by native code whose parser safety is unverified. MB-140 (MCP session ID / stderr logging) was rated Low by its reviewer and is kept Low here.

**CWE references** are given where the reviewers supplied them. They are classification aids, not a substitute for reading the finding.

**Line numbers.** Server line numbers are approximate (±~10 lines) — locate by the quoted code or function name. C++/QML line numbers were re-checked with `grep`/`cat -n` by the reviewers.

---

## 3. Severity summary

### 3.1 By severity (deduplicated)

| Severity | Count |
|---|---|
| Critical | 10 |
| High | 48 |
| Medium | 73 |
| Low | 52 |
| Info | 6 |
| **Total** | **189** |

Raw findings before deduplication: 193 enumerated across the four audit parts (28 + 29 + 75 + 61). Four findings were merged as duplicates of the same underlying issue (see §19): the webhook-signature documentation contradiction (F-04) into MB-003; the QML login-URL finding (F-23) into MB-047; the QML terminal/code-runner auto-start finding (F-21) into MB-082; and the `PathSecurity::isInside()` flaw (V-75 + core M6) into MB-101. 193 − 4 = **189**.

### 3.2 By area

| Area | Critical | High | Medium | Low | Info | Total |
|---|---|---|---|---|---|---|
| Server / billing (`server/`) | 3 | 7 | 11 | 8 | 0 | 29 |
| Agent, tools, MCP, skills (`app/agent/`, `app/tools/`, `app/mcp/`, `app/skills/`) | 5 | 23 | 27 | 17 | 3 | 75 |
| Core, storage, models, inference, hardware, chat (`app/core/`, `app/storage/`, `app/models/`, `app/inference/`, `app/hardware/`, `app/chat/`, `app/api/`, `app/billing/`) | 2 | 13 | 22 | 22 | 2 | 61 |
| Build, CI, release, QML, docs, tests | 0 | 5 | 13 | 5 | 1 | 24 |
| **Total** | **10** | **48** | **73** | **52** | **6** | **189** |

Note: the "Build, CI, release, QML, docs, tests" row is 24, not 28, because four of its 28 findings were merged into findings counted under other areas (F-04 → server, F-21 → agent, F-23 → core, and the `PathSecurity` reference was not a numbered finding in Part 1). The per-part raw counts were 28 / 29 / 75 / 61 = 193; see Appendix B for the tally discrepancies found in the part reports themselves.

### 3.3 By theme (approximate; a finding can belong to more than one theme)

| Theme | Findings |
|---|---|
| Arbitrary code execution / sandbox absence | MB-004–MB-008, MB-038–MB-040, MB-070–MB-072, MB-186–MB-187 |
| Path traversal / symlink / containment | MB-010, MB-020, MB-030–MB-031, MB-052, MB-082–MB-086, MB-089–MB-091, MB-094, MB-101–MB-102, MB-157–MB-160 |
| Supply chain / unsigned code / signing | MB-009, MB-041–MB-046, MB-054–MB-055, MB-096–MB-097, MB-114–MB-117 |
| Billing / entitlements / credits (server) | MB-001–MB-003, MB-011–MB-017, MB-059–MB-069, MB-132–MB-139 |
| Authentication / authorisation / approval gate | MB-021–MB-025, MB-047, MB-063, MB-066, MB-073, MB-109, MB-151 |
| Secrets / tokens / logging | MB-025, MB-027, MB-037, MB-053, MB-062, MB-095, MB-105–MB-108, MB-140, MB-155 |
| Resource exhaustion / DoS | MB-039–MB-040, MB-075, MB-079, MB-087–MB-088, MB-093, MB-096, MB-104, MB-144, MB-169 |
| Prompt injection / untrusted content | MB-032–MB-034, MB-092, MB-110, MB-186, MB-188 |

---

## 4. Wiring caveat — what is latent and what is reachable today

This section is essential context. Several of the most dangerous paths in this document are **latent, not yet wired end to end** in commit `be192b4`. That lowers current exploitability; it does not make the code safe.

**Latent today (dangerous code, no current end-to-end path):**

- Filesystem tools are **not** registered in production. `FileSystemTools::registerTools` is called only in tests (`tests/tools/test_builtin_tools.cpp:77,125`, `tests/agent/test_agent.cpp:159,185,211`), never in production. MB-030, MB-031, MB-083–MB-087 are therefore latent — and Critical the moment anyone calls `registerTools()` with the default (empty) sandbox root.
- `Application.cpp:37` constructs the agent with a null registry and null approval: `m_agent = new AgentOrchestrator(nullptr, nullptr, this)`. The orchestrator creates its own internally; it was not verified as wired to a Prompt-mode gate (MB-152).
- MCP tools are **not** wired into the agent. No consumer feeds MCP tool descriptions/results into a model prompt; `ToolCalling`/`MCPClient` are not instantiated outside `app/mcp/`; `AgentOrchestrator` uses only its own `ToolRegistry`. MB-034 (MCP prompt injection, MB-186), MB-187 (registry `installCommand`), and the MCP tool-shadowing findings are latent until that wiring lands.
- Skills: no instantiation/consumer of `SkillInjector` or `SkillsShClient` was found; `SkillsHubView.qml` only calls `skills.installSkill` / `setSkillEnabled` / `uninstallSkill`. No QML calls `agent.run()`. MB-035–MB-037, MB-092 are latent until skills are wired in.
- `ChatOrchestrator::sendMessage()` is a stub that emits `generationFinished` without doing anything (MB-188). Conversation messages are not fed to any model in the audited code, so the role-spoofing finding (MB-110) is latent until the Phase 2/5 inference/MCP wiring lands.
- `LocalApiServer::start()` binds **no socket at all** (MB-157). There is currently no exposed local API port and no auth bypass — but the stub claims to be running, which is a false signal with high future risk.

**Reachable today (do not dismiss these as latent):**

- Terminal and CodeRunner QML exposure **is real**: `Application.cpp:84-89` exposes `agent`, `terminalSession`, `codeRunner` (and `fileWatcher`, `clipboard`, `notifier`) as QML context properties, and QML calls `session.start(...)`, `session.sendText(...)`, `runner.run(...)` directly with no approval (MB-038, MB-082).
- The server findings are reachable today in any deployment: mock mode (MB-001–MB-003), unauthenticated billing endpoints (MB-011, MB-012), and the credit/plan logic (MB-013–MB-017) are live server code, not stubs.
- The model downloader, engine installer, updater, hardware detection, and Hugging Face client are all reachable from the UI today (MB-009, MB-010, MB-041–MB-049, MB-114–MB-117).
- The approval-gate weaknesses (MB-021–MB-025) are reachable by any QML/JS caller today, because the mutators are `Q_INVOKABLE`.

**Rule for future work:** any change that wires in filesystem tools, MCP tools, skills, the agent loop, or the local API server must first fix the corresponding findings in this document. Wiring first and fixing later is how latent findings become incidents.

---

## 5. Top 10 most urgent findings

| # | ID | Finding | Why it is at the top |
|---|---|---|---|
| 1 | MB-001 | Server fails open into mock mode | One missing env var silently turns a production server into an unauthenticated dev server with free paid access. |
| 2 | MB-005 | No sandbox anywhere | Bounds every execution finding; without it, a single prompt injection is full user-context compromise. |
| 3 | MB-009 | Engine binary downloaded and executed with no verification | Compromised release or MITM = arbitrary native code execution, undetectable by the app. |
| 4 | MB-010 | Model downloader is an arbitrary-file-write primitive | Attacker-controlled Hugging Face repo data can write outside the models directory. |
| 5 | MB-002 | Unauthenticated mock checkout grants paid tier + credits | Free, repeatable, no authentication. |
| 6 | MB-003 | Unsigned webhooks accepted | Anyone who can reach the webhook endpoint can grant tiers/credits. |
| 7 | MB-021 | Approval gate defaults to fail-open `ReadOnly` | The one control meant to stop a model from doing damage is off by default for any tool that forgets to opt in. |
| 8 | MB-044 | Updater signing key is a truncated placeholder | No update can ever install, so no security fix can be shipped safely — and pressure to disable the check is the real danger. |
| 9 | MB-007 | MCP `servers.json` is persistent arbitrary program execution | Any local file write to one JSON file = full RCE on every restart. |
| 10 | MB-041 | Unsigned shared libraries loaded from CWD | Launching the app from Downloads or a shared folder can execute a planted DLL/`.so` during hardware detection. |

---

## 6. Findings — Critical

### MB-001 — Critical, Confirmed — Server fails open into mock mode if Supabase is misconfigured

- **Sources:** Server C1
- **Files:** `server/src/lib/supabase.js:1-25` (`isConfigured()`), `server/src/lib/mock.js:1-30,120-180`, `server/src/app.js:25-35` (`/health` reports `mode: mock`), `server/src/index.js` (prints `database: mock (in-memory)`)
- **CWE:** CWE-1188, CWE-256, CWE-807
- **Evidence:** No `NODE_ENV` check, no startup failure, no warning log when Supabase is unconfigured. The server simply starts in mock mode.
- **Impact:** A production deployment with a typo or a missing environment variable silently serves unauthenticated users with mock auth, plaintext passwords, unsigned webhooks, and free checkouts (MB-002, MB-003, MB-062). This is the root cause that makes the other server Critical findings reachable in production.
- **Remediation:** Fail closed in production if Supabase / Whop / OpenRouter are not configured — refuse to start. Log a loud warning in every mode when mock is active. Never bind a mock-mode server to a public interface.

### MB-002 — Critical in mock mode, Confirmed — Unauthenticated mock checkout grants a paid tier and credits to any user

- **Sources:** Server C2
- **Files:** `server/src/routes/billing.js:35-95` (POST `/billing/checkout` mock branch), `server/src/lib/entitlements.js:30-95` (`activatePlan`)
- **CWE:** CWE-862, CWE-807
- **Evidence:** `resolveCheckoutUser` requires no token. The `periodKey` is deliberately unique per call (`mock-YYYY-MM-${Date.now()}`), so the monthly-grant guard is bypassed by design — repeat calls grant 7 / 25 / 149 credits every time, with no payment.
- **Impact:** Anyone can grant themselves a paid tier and unlimited credits, repeatedly, with no authentication.
- **Remediation:** Remove the mock-grant path outside local development (`NODE_ENV !== production` plus loopback-only), or require a signed-in user plus a dev-only flag. Never grant credits on checkout creation — only on a verified payment webhook.

### MB-003 — Critical in mock mode / if the env flag is set, Confirmed — Unsigned webhooks accepted in mock mode and via `ALLOW_UNSIGNED_WEBHOOKS`

- **Sources:** Server C3; Part 1 F-04 (merged — documentation contradiction, see below)
- **Files:** `server/src/routes/billing.js:168-210` (POST `/billing/webhook`), `server/.env.example:20-25`; docs: `docs/SECURITY.md:11-16`, `docs/API.md:61`, `docs/BILLING.md:20-29`
- **CWE:** CWE-347, CWE-862
- **Evidence:** If verification fails, unsigned webhooks are still accepted when `(!isConfigured() && reason === 'webhook_secret_not_configured') || ALLOW_UNSIGNED_WEBHOOKS === 'true'`, and `handleWhopEvent` runs anyway. The response echoes `verified: verification.ok` alongside `ok:true`, so a caller can see `verified:false` and `ok:true` together. `scripts/smoke.mjs` relies on this bypass. The documentation contradicts itself: `docs/SECURITY.md` says the webhook signature is *not* implemented and the route returns `{ok:true}` without verifying `X-Whop-Signature`, while `docs/API.md` and `docs/BILLING.md` say it *is* verified with `crypto.timingSafeEqual` / `safeEqual()` using `WHOP_WEBHOOK_SECRET`, and that mock mode accepts unsigned webhooks. The server code is the ground truth: verification exists, but the unsigned bypass exists too.
- **Impact:** Anyone who can reach the webhook endpoint can forge billing events and grant tiers/credits (MB-013, MB-017). The contradictory docs mean nobody can tell from documentation alone which behaviour is intended.
- **Remediation:** Reject unsigned webhooks unconditionally when `NODE_ENV=production`. Gate any dev bypass behind a dev-only flag plus loopback-only binding. Never echo `verified:false` alongside `ok:true`. Fix the documentation to match the code, and add a real integration test that an unsigned webhook is rejected in production mode.

### MB-004 — Critical, Confirmed — TerminalTool: `shell=true` is intentional arbitrary shell execution, with no allowlist

- **Sources:** Part 3 V-01
- **Files:** `app/tools/TerminalTool.cpp:104-106, 140-152, 294-295`
- **CWE:** CWE-78, CWE-88
- **Evidence:** `program="/bin/sh"; processArgs << "-c" << command;` (Windows: `cmd.exe /d /s /c`). No allowlist, denylist, or metacharacter filtering.
- **Impact:** Any model or prompt-injected command runs with the app user's full privileges: `;`, `&&`, `|`, `$()`, backticks, `sudo`, `curl | sh`, `rm -rf ~`.
- **Remediation:** Remove shell mode for agent use, or default-deny it behind explicit human approval plus an allowlisted command grammar. Never expose it to model-generated input. Run only inside a real sandbox (MB-005).

### MB-005 — Critical, Confirmed — No sandbox in any scoped file

- **Sources:** Part 3 V-02
- **Files:** All scoped agent/tool files; `app/main.cpp:9-38`
- **CWE:** CWE-250, CWE-693
- **Evidence:** No chroot, container, seccomp, AppArmor, setuid/setgid drop, namespace, `setrlimit`, `setpgid`/`killpg`, or privilege drop anywhere. The only confinement is TerminalTool's lexical cwd check (MB-020) and CodeRunner's throwaway temp dir — neither restricts the child process, filesystem, or network. `main.cpp` passes `argv` straight to Qt, loads QML, and runs the full agent with user privileges; there is no `--safe-mode`.
- **Impact:** Every child (shell, interpreter, compiled C++ binary, MCP server) runs as the app user with full filesystem and network access: `~/.ssh`, credentials, other projects, exfiltration, destructive writes.
- **Remediation:** A real sandbox: separate UID, chroot/container/bwrap/firejail, seccomp plus `setrlimit`, no network by default, read-only filesystem except a scratch dir. Add a `--safe-mode` (tools disabled / AutoDeny). Do not rely on cwd alone.

### MB-006 — Critical, Confirmed — CodeRunner: unsandboxed arbitrary code execution (Python / JavaScript / C++)

- **Sources:** Part 3 V-03
- **Files:** `app/tools/CodeRunner.cpp:71-149, 204-205, 225-226, 254`
- **CWE:** CWE-94, CWE-78
- **Evidence:** Writes code to a `QTemporaryDir` source file, compiles C++ with `-std=c++17 -O0 -pipe source -o runProgram`, runs it, and calls `setProcessEnvironment(systemEnvironment())`. No interpreter hardening (`python3 -I/-s`), no import/module restriction, no filesystem/network restriction.
- **Impact:** A snippet can `import os/subprocess/socket`, read/write anywhere, spawn processes, use the network, read secrets, fork-bomb, and run native code as the app user.
- **Remediation:** Same sandbox as MB-005, minimal environment (`-I`, cleared `PYTHONPATH`/`NODE_OPTIONS`), no network, CPU/memory/time/file-size rlimits, strict output/disk quota. Treat snippet execution as untrusted by default.

### MB-007 — Critical, Confirmed — MCP: persistent arbitrary program execution via `servers.json` (config-as-code, no integrity or allowlist)

- **Sources:** Part 3 V-04
- **Files:** `app/mcp/MCPServerManager.cpp:91-122` (`loadServers`), `:71-85` (`configFromJson`), `:214,250` (`connectServer` → `connectStdio(cfg.command, cfg.args, cfg.workingDir)`); `app/mcp/MCPClient.cpp:164-165`
- **CWE:** CWE-78, CWE-829, CWE-732
- **Evidence:** `command` / `args` / `workingDir` / `transport` / `url` are taken as-is; no allowlist, canonicalisation, or signature. `enabled` defaults to true, so a planted entry executes on the next connect. `m_proc.start(m_stdioCommand, m_stdioArgs)` is argv-based, with no shell — the risk is the arguments and the command themselves, not shell parsing.
- **Impact:** Any local file write to `<AppData>/mcp/servers.json` (malware, a malicious backup/restore, a shared profile, another vulnerable app) gives full user-context RCE, persisted across restarts.
- **Remediation:** Treat `servers.json` as executable config: store it `0600`, HMAC/sign it with a per-install key (or store commands in the OS keyring / protected store); validate the command against an allowlist of known binaries / canonical absolute paths, reject relative and `PATH`-resolved names, validate `workingDir` against an approved root, and require explicit user confirmation before auto-connecting a server whose config changed since last approval.

### MB-008 — Critical (Suspected RCE); arbitrary-URL acceptance Confirmed — Git push/pull accept an arbitrary remote URL, including the `ext::` command-execution transport

- **Sources:** Part 3 V-05
- **Files:** `app/tools/GitTools.cpp:453-477` (push), `:500-522` (pull); `runGit()` `:119-137`
- **CWE:** CWE-88, CWE-78, CWE-918
- **Evidence:** `remote = args.value("remote").toString("origin"); if (remote.startsWith('-')) reject; argv << remote;` No allowlist, no check against `git remote`, no scheme check. `runGit()` sets only `-c color.ui=false`; it does not set `-c protocol.allow=…`, `protocol.ext.allow`, or `credential.*`.
- **Impact:** `remote="ext::sh -c <cmd>"` is the classic git `ext::` transport RCE (git itself spawns the command). Also `https://attacker`, `git@attacker`, `file:///`, and local paths allow exfiltration/import, SSRF to internal git/HTTP services, and pushing to a local repo outside the intended remote. Pull fetches and merges attacker content. The `ext::` execution itself was not runtime-proved (no PoC run), hence Suspected for the RCE; the arbitrary-URL acceptance is Confirmed.
- **Remediation:** Resolve the remote only via a `git remote` allowlist; reject anything not in the list, anything containing `://`, `:`, `/`, `\`, or starting with `ext:`, `file:`, or `-`. Pass `-c protocol.allow=https,ssh,git` (`protocol.ext.allow=never`, `protocol.file.allow=never` unless local remotes are explicitly required) in `runGit()`.

### MB-009 — Critical, Confirmed — Engine binary downloaded and executed with no hash, signature, or size verification

- **Sources:** Part 4 C1
- **Files:** `app/inference/EngineInstaller.cpp:48-55, 114-163, 169-195`
- **CWE:** CWE-494, CWE-829
- **Evidence:** `checkLatest()` (`:48`) trusts the GitHub API JSON verbatim — `QNetworkRequest req{QUrl("https://api.github.com/repos/ggml-org/llama.cpp/releases/latest")}`. `install()` (`:154-160`) takes the asset URL from that JSON and fetches it, with no hash or signature anywhere. `onDownloadFinished()` (`:~181,187`) writes the bytes, then extracts and loads the native `.dll`/`.so`. `grep -ri "sha256|hash|signature|verify|checksum" app/models app/inference` returned zero hits. `isInstalled()` (`:28-41`) checks only that `llama.dll` / `libllama.so` *exists*, never its integrity.
- **Impact:** A compromised or malicious `llama.cpp` release, a compromised GitHub account/token, or a TLS MITM with a trusted CA gives arbitrary native code execution in the app's context — the extracted library is subsequently loaded as the inference engine. The app has no way to detect a substituted binary.
- **Remediation:** Verify the release's published SHA-256 (and ideally a Sigstore/GPG signature) against a pinned expected value before extraction; fail closed on mismatch. Download to a temp file, verify, then atomically rename. Also verify the installed library hash at load time.

### MB-010 — Critical, Confirmed — `ModelDownloader::download()` is an unauthenticated arbitrary-file-write primitive; the existing path guard is imported but never called

- **Sources:** Part 4 C2
- **Files:** `app/models/ModelDownloader.cpp:2, 20-29`; `app/models/ModelDownloader.h:14`; `app/ui/ModelsView.qml:288-291`
- **CWE:** CWE-22, CWE-73, CWE-434, CWE-918
- **Evidence:** `#include "core/PathSecurity.h"` (line 2) is imported and never used. `void ModelDownloader::download(const QString &url, const QString &destPath)` is `Q_INVOKABLE`. It does `m_file.setFileName(destPath); QDir().mkpath(fi.absolutePath());` (creates arbitrary directories) and `m_file.open(QIODevice::Append | QIODevice::WriteOnly);` (follows symlinks, appends). There is no validation of `url` (any scheme/host) or `destPath` (absolute, `..`, symlink) in this file, and `grep` confirms `PathSecurity::isInside` / `sanitizeFileName` are called nowhere in `app/models` or `app/inference`. The QML caller only partially mitigates it: `fileName = modelData.name.split('/').pop()` strips `/` directories but not `\` (the Windows separator) or a bare `..` segment — and the filename/URL come straight from the Hugging Face API listing of an arbitrary repo.
- **Impact:** A malicious Hugging Face repo file listing (attacker-controlled repo data) or any QML-accessible caller can write downloaded bytes outside the models directory, create arbitrary directories, and append to existing files. `url` is likewise unrestricted — an arbitrary-host fetch (client-side SSRF / file write).
- **Remediation:** In `download()`, enforce an allowlist of scheme `https` and host `huggingface.co` (plus `cdn-lfs.huggingface.co`); require `PathSecurity::isInside(Paths::modelsDirectory(), destPath)` after `QFileInfo::canonicalFilePath` resolution, reject symlinks; sanitise the filename with `PathSecurity::sanitizeFileName()`; open with `QIODevice::WriteOnly` (truncate) on a `.part` file and rename on verified completion. See also MB-048, MB-049, MB-101, MB-130.

---

## 7. Findings — High

### 7.1 Server / billing — High

### MB-011 — High, Confirmed — GET `/billing/entitlements?userId=…` discloses any user's billing data, with no authentication

- **Sources:** Server H1
- **Files:** `server/src/routes/billing.js:115-150`
- **CWE:** CWE-862, CWE-863, CWE-200
- **Evidence:** The token is optional; if a token is present `requireUser` is used, otherwise the query `userId` is used, with no ownership check, and the service-role `getProfile` is called. The response returns tier, features, credits balance / lifetime granted / lifetime used, subscription status and period end. There is no rate limiter on the route. In mock mode IDs are deterministic (see MB-062), so any email's ID is computable and enumeration is trivial.
- **Impact:** Anyone can read any user's billing data.
- **Remediation:** Require `requireUser` always, ignore the `userId` query parameter, and add a rate limiter.

### MB-012 — High, Confirmed — POST `/billing/checkout` accepts an unauthenticated, attacker-chosen `userId`

- **Sources:** Server H2
- **Files:** `server/src/routes/billing.js:35-60, 235-265` (`resolveCheckoutUser`)
- **CWE:** CWE-862, CWE-639
- **Evidence:** If a token is present it returns `requireUser`; otherwise, if a `userId` is given, it only checks UUID format and that the profile exists, and returns `{id: userId}`.
- **Impact:** In live mode anyone can create a Whop checkout with metadata `user_id=<victim>`, so payment is attributed to the victim and the webhook grants the victim tier/credits. In mock mode this is the entry point for MB-002.
- **Remediation:** Require authentication for checkout in all modes and derive `userId` solely from the verified token.

### MB-013 — High, Confirmed — Entitlements are webhook-only: no expiry check, no Whop verification, tier persists forever if a webhook is missed

- **Sources:** Server H3
- **Files:** `server/src/lib/entitlements.js:265-320` (`handleWhopEvent`), `server/src/routes/ai.js:110-180`, `server/src/lib/credits.js:230-280`
- **CWE:** CWE-862, CWE-672, CWE-347
- **Evidence:** `handleWhopEvent` trusts the webhook payload alone, never calls Whop to verify membership, never checks that `current_period_end` is in the future, and never checks `status`. `activatePlan` / `deactivatePlan` only run on webhooks. Nothing in `/ai/chat`, `/user/entitlements`, or `/billing/entitlements` checks `subscription_status` or `current_period_end`. Classification is type-substring only: `/(activat|created|went_valid|renew|resumed|updated|changed)/` — so a `membership_updated` with `status=canceled` is treated as an activation. `markPaymentFailed` intentionally keeps the tier, with no grace-period expiry.
- **Impact:** Cancelled or expired subscriptions keep working; a single forged or misclassified webhook event grants a tier indefinitely.
- **Remediation:** Enforce `current_period_end > now` and `subscription_status === 'active'` on every gated request (with a short explicit grace period for `past_due`), verify memberships server-side against Whop on activation and reconcile periodically, and classify by (type, status) together.

### MB-014 — High, Confirmed at code level; live exploitability Suspected — AI credit settlement is check-then-charge (TOCTOU): concurrent or over-budget requests can stream for free

- **Sources:** Server H4
- **Files:** `server/src/routes/ai.js:110-180, 300-380` (POST `/ai/chat`, `proxyStream`)
- **CWE:** CWE-367, CWE-400
- **Evidence:** The balance is pre-checked against estimated credits, then the upstream OpenRouter call is streamed, and only afterwards may `recordUsage` throw `insufficient_credits`. In `proxyStream` a settlement failure is swallowed into an SSE event after the full response has already been streamed, and the server still writes `modelbench.usage` plus `[DONE]`. Non-streaming: `recordUsage` throwing after the upstream call means the server paid OpenRouter but returns 500 and charges nothing. The estimate heuristic is `estimatePromptTokens: text.length/4`, and actual tokens can exceed the estimate. The race was not executed live, hence Suspected for live exploitability.
- **Impact:** Concurrent requests can overdraw credits and stream for free; the server can pay for upstream usage it never charges for.
- **Remediation:** Reserve/hold credits before the upstream call (atomic `deduct_credits` for the estimate, refund the difference after actual usage), or abort the stream and do not deliver content if settlement fails. Make the estimate a conservative upper bound and use the provider's real prompt-token count when available.

### MB-015 — High, Confirmed — `tierForPlanId` grants a tier on substring / name match — a cheap or misnamed Whop plan can grant Teams

- **Sources:** Server H5
- **Files:** `server/src/config/pricing.js:95-130` (`tierForPlanId`), `server/src/lib/entitlements.js:200-235` (`normalizeWhopEvent`)
- **CWE:** CWE-807, CWE-862
- **Evidence:** Exact match against configured IDs happens first (good), then alias normalisation, then `if (lowered === tier || lowered.includes(tier)) return tier`. The webhook feeds untrusted plan data: `tierForPlanId(planId) || tierForPlanId(planName) || normalizeTier(metadata.tier)`.
- **Impact:** Any Whop plan ID or name containing `hobby` / `scale` / `teams` — for example `plan_teams_free_trial`, "Teams Trial", "scale-up" — maps to that tier and grants its full monthly credits (149 for Teams). Smoke-test IDs `plan_hobby` / `plan_scale` work without a configured mapping, masking misconfiguration.
- **Remediation:** Allowlist only — exact match against configured `WHOP_PLAN_*` env vars, reject `unknown_plan`, remove the `includes` loop and the plan-name fallback.

### MB-016 — High if `*` is ever configured; Medium as shipped, Confirmed — CORS: `*` in the allowlist plus `credentials:true` means any site can make credentialed calls

- **Sources:** Server H6
- **Files:** `server/src/app.js:80-115` (`corsOptions`)
- **CWE:** CWE-942, CWE-639
- **Evidence:** `wildcard = allowlist.includes('*')`; `if (!origin) callback(null, true)`; `if (wildcard || allowlist.includes(origin)) callback(null, true)`; `credentials: true`. With `credentials:true`, `cors` reflects the Origin; the wildcard does not fail safe. The default allowlist (`localhost:3000`, `SITE_URL`) is safe; the danger is the supported `*` configuration.
- **Impact:** If `*` is ever configured, any website can make credentialed cross-origin calls to the API.
- **Remediation:** Reject `*` whenever `credentials:true` (fail startup if both are set), require an explicit origin allowlist in production, and never reflect an arbitrary origin with credentials.

### MB-017 — High, Confirmed at code level; live exploitability Suspected — Credit grants are not atomic, and the period key is webhook-controlled — double-grant / credit farming

- **Sources:** Server H7
- **Files:** `server/src/lib/entitlements.js:30-95` (`activatePlan`), `:285-310` (period-key derivation)
- **CWE:** CWE-367, CWE-807
- **Evidence:** No transaction, no `FOR UPDATE`, no conditional update `.eq('credit_period_key', oldPeriodKey)`. Two deliveries with different event IDs (Whop retries, `membership_activated` + `payment_succeeded`) can both read the old key and both grant. The period key is derived from webhook-supplied `period_start`: `periodKey = normalized.periodStart ? currentPeriodKey(new Date(normalized.periodStart)) : currentPeriodKey()`. A forged/replayed event with a different `YYYY-MM` in `period_start` gets a fresh grant each time.
- **Impact:** Duplicate grants from retries, and credit farming from forged events with manipulated period starts.
- **Remediation:** Make the grant plus period-key update a single atomic DB operation (a `SECURITY DEFINER` RPC `UPDATE … WHERE credit_period_key IS DISTINCT FROM $key`, granting only when `row_count=1`, or `SELECT … FOR UPDATE`). Derive the period key from server time or the Whop-verified `current_period_end`, never from webhook `period_start`.

### 7.2 Agent, tools, MCP, skills — High

### MB-018 — High, Confirmed — TerminalTool non-shell mode still allows arbitrary binaries; the shell flag is not a security boundary

- **Sources:** Part 3 V-06
- **Files:** `app/tools/TerminalTool.cpp:115-120, 153-155, 294-295`
- **CWE:** CWE-78, CWE-426
- **Evidence:** `approvalLevelFor()` classifies by the bool alone: shell ? Dangerous : Execute. In non-shell mode `program=command; processArgs=argv;` with no allowlist.
- **Impact:** `command="/bin/sh"` with `args=["-c","…"]`, `python3 -c …`, `rm`, `sudo`, or `curl` achieve shell-equivalent execution while classified only as Execute.
- **Remediation:** Allowlist executable paths/names (absolute, canonicalised); classify any shell/interpreter (`sh`, `bash`, `python`, `node`, `powershell`, `sudo`, `su`) as Dangerous regardless of the `shell=` flag; resolve via a pinned safe `PATH`.

### MB-019 — High, Confirmed — Environment injection and credential leakage via inherited and caller-controlled environment (cross-cutting)

- **Sources:** Part 3 V-07
- **Files:** `app/tools/TerminalTool.cpp:181-191,182, 401-410,515-522,576-587` (TerminalSession, incl. Windows environment block); `app/tools/CodeRunner.cpp:225` (run; compile inherits the default env, no `setProcessEnvironment` in `startCompile` `:159-205`); `app/tools/GitTools.cpp:135-137` (`runGit`, no `setProcessEnvironment`, no absolute git path); `app/mcp/MCPClient.cpp:164-165` (no `setProcessEnvironment` anywhere in `app/mcp/`; `MCPServerConfig` has no `env` field)
- **CWE:** CWE-426, CWE-200, CWE-114, CWE-212
- **Evidence:** `QProcessEnvironment env = systemEnvironment();` — only empty / `=` / NUL keys are rejected, then `env.insert(key,value)` for any other key. The Windows TerminalSession does no key validation before `key+"="+value`. Git does not clear `GIT_DIR`, `GIT_WORK_TREE`, `GIT_CONFIG_PARAMETERS`, `GIT_SSH_COMMAND`, `GIT_ASKPASS`, or `GIT_CONFIG_NOSYSTEM`.
- **Impact:** (a) The child inherits the app's entire environment — `env`/`printenv` dumps API keys/tokens the parent holds; every stdio MCP server receives them. (b) A caller can override `PATH` (hijack resolution), `LD_PRELOAD`/`LD_LIBRARY_PATH`/`DYLD_*` (hijack native binaries), `PYTHONPATH`/`PYTHONSTARTUP`/`NODE_OPTIONS` (code injection), `SHELL`, `IFS`. (c) A hostile parent environment can redirect git's repo/SSH/config.
- **Remediation:** Start from a minimal allowlisted environment, strip `LD_*`/`DYLD_*`/`PYTHON*`/`NODE_OPTIONS`/`PATH` (set a pinned safe `PATH`), and never pass the parent environment wholesale to untrusted children. Add a per-MCP-server env allowlist. For git, use a fixed absolute path and a clean environment dropping `GIT_*`/`SSH_*`/`LD_PRELOAD`.

### MB-020 — High, Confirmed — cwd / path confinement is lexical only, opt-in, and symlink-bypassable (cross-cutting)

- **Sources:** Part 3 V-08
- **Files:** `app/tools/TerminalTool.cpp:50-61,158-177,200-203`; `app/tools/GitTools.cpp:42-52` (`isInsideRoot`), `:55-71` (`pathArgument`); `app/core/PathSecurity.cpp:6-13` (reference)
- **CWE:** CWE-22, CWE-59, CWE-73
- **Evidence:** `isInsideRoot()` does `QDir::cleanPath(root/candidate)` plus a prefix check only. In TerminalTool, cwd is resolved against `m_root` only if `cwdInput` is non-empty; the check is skipped if `m_root` is empty; an omitted cwd inherits the app cwd (`:200-203`). `cleanPath()` normalises `..` lexically but does **not** resolve symlinks. `PathSecurity::isInside()` has the same flaw (`absolutePath`, no canonicalisation) and is not even used — TerminalTool and GitTools duplicate the logic locally.
- **Impact:** A symlink inside the root pointing outside (`ln -s /etc link; cwd=link, path="link/passwd"`) passes. With `m_root` empty or cwd omitted, there is no confinement at all. The program path is never checked against the root (MB-018).
- **Remediation:** Canonicalise both root and candidate with `QFileInfo::canonicalFilePath()`, re-check after resolution, require a non-empty root, and default cwd to the root when omitted. Fix `PathSecurity::isInside()` the same way (canonical, case-insensitive on Windows/macOS, proper separator-boundary check — it also has a boundary bug for root `/`) and use that one helper everywhere; never adopt the current `isInside()` as-is. See MB-101.

### MB-021 — High, Confirmed — Approval gate trusts the tool's self-reported risk level; the default is fail-open `ReadOnly`

- **Sources:** Part 3 V-09
- **Files:** `app/tools/ToolRegistry.h:40,42-44`; `app/tools/ToolRegistry.cpp:208,213`; `app/tools/ToolApproval.cpp:19-21,100-102`
- **CWE:** CWE-862, CWE-284
- **Evidence:** `virtual approvalLevel() const { return ApprovalLevel::ReadOnly; }` / `approvalLevelFor(args)` defaults to it; `call.level = t->approvalLevelFor(args); if (m_approval && requiresApproval(call.level)) gate;` and `requiresApproval` returns `level != ReadOnly`.
- **Impact:** Any tool that forgets to override `approvalLevel()`, or whose `approvalLevelFor(args)` misclassifies a dangerous operation, runs with **no** user confirmation. There is no central allowlist, no minimum level per tool name, and no registry-side override.
- **Remediation:** Default `approvalLevel()` to Dangerous (fail closed). Add a registry-side table mapping tool name to a minimum `ApprovalLevel` that a tool cannot lower. Treat `approvalLevelFor()` as advisory only. Add a test that every registered tool declares non-`ReadOnly` unless explicitly allowlisted as read-only.

### MB-022 — High, Confirmed — `setApproval(nullptr)` silently disables the approval gate

- **Sources:** Part 3 V-10
- **Files:** `app/tools/ToolRegistry.cpp:24-39,213-224`; `app/tools/ToolRegistry.h:137`
- **CWE:** CWE-862
- **Evidence:** `setApproval` accepts `nullptr` with no check. `execute()`: `if (m_approval && requiresApproval(...)) gate;` then `beginExecution(id);` is reached immediately when `m_approval==nullptr`. This undoes the constructor fail-safe (`if (!approval) approval = new ToolApproval(this);` `:18-19`).
- **Impact:** One call removes the entire approval gate.
- **Remediation:** Reject `nullptr` in `setApproval()` (ignore it, or `Q_ASSERT`/log and keep the existing gate). In `execute()`, fail closed — if `m_approval==nullptr`, fail the call with `approval_unavailable` instead of executing.

### MB-023 — High, Confirmed in code; exploitability Suspected — Unauthenticated, session-wide, persistent-for-session bypasses: `AutoApprove`, `alwaysAllow`, scoped allowances — all `Q_INVOKABLE` / `Q_PROPERTY`-writable

- **Sources:** Part 3 V-11
- **Files:** `app/tools/ToolApproval.h:37,69,75,80,111`; `app/tools/ToolApproval.cpp:13-17,104-117,189-216,250-253`
- **CWE:** CWE-862, CWE-732
- **Evidence:** `Q_PROPERTY(ApprovalMode mode READ/WRITE setMode);` — `setMode()` changes the mode with no auth and no log. `request()` checks bypasses **before** the mode switch: if `alwaysAllowed` contains the tool name, auto-decide true; if a path scope is allowed, auto-decide true; in `AutoApprove` mode, auto-decide true. `Q_INVOKABLE setAlwaysAllowed`, `setScopedAllowance`, `clearPermissions`, `clearApprovalLog`.
- **Impact:** A single call to `setMode(AutoApprove)`, `setAlwaysAllowed("terminal",true)`, or a broad `setScopedAllowance("write_file","*",true)` permanently (for the session) exempts that tool from prompting — for all arguments/paths/commands, with no expiry and no per-call re-confirmation. `setMode` writes no audit-log entry. `clearPermissions` / `clearApprovalLog` erase grants and the audit trail. The default mode is safe (`Prompt`, `ToolApproval.h:111`).
- **Remediation:** Remove `Q_INVOKABLE` from the mutators (or gate them behind an explicit user-confirmed privileged UI only). Require re-confirmation for `AutoApprove` (never persist it, warn loudly, auto-revert to Prompt). Scope `alwaysAllow` per tool **and** argument fingerprint, with expiry. Log every mode/permission change into a non-clearable audit log.

### MB-024 — High, Confirmed in gate logic; end-to-end exploit Suspected — Scoped allowance is a weak path check: only the first path-like argument, lexical only, no canonicalisation, no sandbox root

- **Sources:** Part 3 V-12
- **Files:** `app/tools/ToolApproval.cpp:108-112,141-149,218-235`
- **CWE:** CWE-22, CWE-59, CWE-73
- **Evidence:** `pathFromArgs()` returns the **first** match among `{"path","cwd","file","uri"}`; all other path arguments are ignored. `isScopeAllowed()` does `QDir::cleanPath(path)` plus `QDir::match(rule.pathGlob, clean)` — a filename glob, with no root/boundary. `grep PathSecurity|isInside|sanitize|canonical` across `main`/`Application`/`ToolApproval`/`ToolRegistry` returns zero hits. `permissionRules()` renders an always-allowed tool as `pathGlob:"*"`.
- **Impact:** A tool taking two paths (source/destination, cwd+path) is approved on the first only; the second is unchecked. For terminal/shell, the scope key is cwd only — approving "terminal in `C:/repo/*`" approves **any** command there. No symlink resolution; relative paths are matched without resolving against a sandbox root, and there is no root at all.
- **Remediation:** Resolve **every** path-like argument (all keys, nested args) with `canonicalFilePath()`; enforce containment in an explicit per-tool sandbox root via the fixed `PathSecurity::isInside()`; never scope-approve a command-execution tool by cwd alone — require an explicit command allowlist, or deny scoped approval for Execute/Dangerous entirely.

### MB-025 — High, Confirmed — Secret redaction in `makePreview()` is defeated: raw arguments are emitted alongside the preview, hints are incomplete, nested values are copied verbatim

- **Sources:** Part 3 V-13
- **Files:** `app/tools/ToolApproval.cpp:34-90,122-123`; `app/tools/ToolRegistry.cpp:211,217-219`
- **CWE:** CWE-200, CWE-359
- **Evidence:** Hints (`:37-42`): `token`, `password`, `secret`, `apikey`, `api_key`, `authorization`, `credential`, `privatekey` — missing `key` alone, `passwd`, `pwd`, `bearer`, `session`, `cookie`, `private_key` (the underscore breaks the `privatekey` match), `access-token`, `auth` alone. Nested objects/arrays are copied verbatim, with no recursion (`:79-81`). The same signals carry unredacted args: `emit approvalRequested(callId, toolName, args, makePreview(...), level);`, `emit toolApprovalRequired(id, name, args, preview, level);`, `emit toolStarted(id, name, args);` (before approval).
- **Impact:** Any subscriber (QML UI, logging, agent observation) receives full credentials/tokens in `args`, even though the preview is redacted. Secrets in nested objects (`{"config":{"api_key":"…"}}`, `{"headers":{"Authorization":"…"}}`) leak through the preview itself. Truncation at 400 chars / 24 array items still exposes the first 400 chars.
- **Remediation:** Never emit raw args on approval/UI signals — emit only the redacted preview (or a hash). Make redaction recursive over nested objects/arrays, expand the hint list (`key`, `passwd`, `pwd`, `bearer`, `session`, `cookie`, `auth`, `private_key`, `access_token`, hyphen/underscore variants), and additionally redact high-entropy / known token shapes. Audit QML/log sinks for stored args.

### MB-026 — High, Confirmed — MCP: SSRF and bearer token to arbitrary / internal / plaintext-HTTP endpoints

- **Sources:** Part 3 V-14
- **Files:** `app/mcp/MCPClient.cpp:77-86` (`connectHttp`), `:216` (`openSseStream`), `:265` (`sendHttpPost`); `app/mcp/MCPServerManager.cpp:252` (`connectHttp(cfg.url, cfg.authToken, cfg.customHeaders)`)
- **CWE:** CWE-918, CWE-319, CWE-200
- **Evidence:** No `QUrl::isValid()`, scheme, host, or private-network check anywhere in `app/mcp/` — `grep scheme|host()|localhost|127\.|isValid` returns nothing except URL constructions. `m_httpUrl=url; m_httpBearer=bearerToken;` with no validation; `startHttp()`; `req.setRawHeader("Authorization", ("Bearer "+m_httpBearer));`. No `RedirectPolicyAttribute` is set, so whether the Bearer token is forwarded on a cross-host redirect could not be confirmed (Suspected; contrast `ModelDownloader.cpp:36`, which sets `NoLessSafeRedirectPolicy`).
- **Impact:** A crafted/imported server config (or a pasted URL) makes the app issue authenticated requests to `http://localhost/`, `127.0.0.1`, `169.254.169.254`, RFC-1918 addresses, or any `http://` host — leaking the bearer token in cleartext and reaching internal MCP/admin/metadata services.
- **Remediation:** Require `https` (allow `http` only as an explicit localhost opt-in with a warning); block loopback/link-local/private/reserved addresses via `QHostAddress::isLoopback` / `isPrivateUse` plus a DNS-resolution check (re-checked after redirect); validate the URL before storing/connecting; never send `Authorization` on `http://`; set `NoLessSafeRedirectPolicy` explicitly.

### MB-027 — High, Confirmed — MCP: bearer tokens and custom headers stored in plaintext, world-readable by default

- **Sources:** Part 3 V-15
- **Files:** `app/mcp/MCPServerManager.cpp:54-64` (`configToJson`: `o["authToken"]=c.authToken; o["customHeaders"]=…`), `:124-137` (`saveServers`)
- **CWE:** CWE-256, CWE-312, CWE-922
- **Evidence:** `QFile file(m_storagePath+"/servers.json"); file.write(QJsonDocument(arr).toJson(Indented));` — no `QFile::setPermissions()`, no encryption, no keyring anywhere in scope. Default `QFile` creation is `0666 & ~umask` (typically `0644`). `MCPServerConfig` (`MCPServerManager.h:23-24`): `authToken`, `customHeaders` (which may contain `Authorization`/`Cookie`).
- **Impact:** Any other local user/process (or backup/sync) reading `servers.json` harvests MCP bearer tokens.
- **Remediation:** Store tokens in the OS credential store (Qt Keychain / Secret Service / Keychain), never in `servers.json`. If the file must hold them, encrypt with a per-install key and call `QFile::setPermissions(0600)` immediately after creation, and redact tokens from `getServers()`/logs.

### MB-028 — High, Confirmed — Git: `git_branch` — no option-injection validation on branch / startPoint / newName

- **Sources:** Part 3 V-16
- **Files:** `app/tools/GitTools.cpp:332-377`
- **CWE:** CWE-88, CWE-20
- **Evidence:** `argv << "branch" << "--list"; if branchName non-empty argv << branchName;` create: `argv << "branch" << branchName; if start non-empty argv << start;` checkout: `argv << "checkout" << branchName;` delete: `argv << "branch" << ("-D"/"-d") << branchName;` rename: `argv << "branch" << "-m" << branchName << newName;`. No `startsWith('-')` check (unlike diff/push/pull), no `--` separator, no `git check-ref-format` validation.
- **Impact:** A single token `--opt=value` is parsed by git before ref validation — for example checkout `"--orphan=evil"` creates an orphan branch, `"--force"`/`"-"` forces/discards or switches unexpectedly; `startPoint`/`newName`/`list` can similarly inject `--track`, `--set-upstream-to=…`, `--format=…`, `--sort=…`. A checkout `<name>` that matches a file can discard uncommitted changes to that file (branch/path ambiguity).
- **Remediation:** Validate every ref with `git check-ref-format --branch` (or a strict allowlist regex such as `^[A-Za-z0-9._/-]+$` plus ref-format rules), reject any value starting with `-` or containing `..`, `@{`, whitespace/control characters, and use explicit `git switch` / `git checkout -b` forms instead of passing user text as a positional.

### MB-029 — High, Confirmed — Git: `git push` / `pull` — branch passed verbatim as a refspec

- **Sources:** Part 3 V-17
- **Files:** `app/tools/GitTools.cpp:462-470` (push), `:512-520` (pull)
- **CWE:** CWE-88, CWE-284
- **Evidence:** `branch = args.value("branch"); if non-empty { if branch.startsWith('-') reject; argv << branch; }` — only the leading-`-` check. `:`/`+` are not checked.
- **Impact:** `branch` is not a branch name, it is a refspec. `branch=":main"` deletes the remote branch on push; `"+main:main"` force-pushes, bypassing the explicit `forceWithLease` boolean (`:471-474`); `"main:refs/heads/other"`, `refs/tags/v1:refs/tags/v1`, `HEAD:refs/heads/x` push any local ref to any remote ref/tag. Pull refspecs can fetch/overwrite local refs.
- **Remediation:** Validate the branch as a single ref (same check as MB-028, reject `:` and a leading `+`), construct the refspec server-side as `<branch>:<branch>` (or `refs/heads/<branch>:refs/heads/<branch>`), and never pass user text as a refspec. Deletion/force-push require a separate explicit flag/approval, not a branch string.

### MB-030 — High, Confirmed — Filesystem: sandbox confinement is opt-in and defaults to off

- **Sources:** Part 3 V-18
- **Files:** `app/tools/FileSystemTools.h:89-90`; `app/tools/FileSystemTools.cpp:67-68,82-95,611-612`
- **CWE:** CWE-22, CWE-668
- **Evidence:** `void registerTools(ToolRegistry &registry, const QString &sandboxRoot = {}, …` and `if (!m_root.isEmpty()) { if (!isInsidePath(root,resolved)) reject }`. Relative paths resolve against the process CWD when the root is empty (`:82-84`).
- **Impact:** With the default empty root, `read_file` / `write_file` / `edit_file` / `list_dir` / `search_code` accept any absolute path the app user can access — `~/.ssh/id_rsa`, `~/.env`, `~/.bashrc`, `/etc/passwd` — and `write_file` can overwrite them. There is no sensitive-file allowlist/denylist anywhere in scope. Latent today (tools are not registered in production) and Critical the moment anyone wires `registerTools()` with the default argument.
- **Remediation:** Remove the `{}` default; require a non-empty, existing, canonicalised root and fail closed if it is empty/non-existent. Add an explicit denylist (`.ssh/`, `*.pem`, `.env`, credentials) even inside the root, or an allowlist of project file types.

### MB-031 — High, Confirmed — Filesystem: `search_code` follows symlink files outside the sandbox, bypassing `resolvePath` entirely

- **Sources:** Part 3 V-19
- **Files:** `app/tools/FileSystemTools.cpp:553-565`
- **CWE:** CWE-59, CWE-22, CWE-73
- **Evidence:** `QDirIterator it(root, filePattern, QDir::Files | NoDotAndDotDot, Subdirectories);` — no `FollowSymlinks`, no per-file check — then `QFile file(filePath); file.open(ReadOnly); file.read(maxFileBytes);`. Only the search root passes through `resolvePath()` (`:516`). Every descendant is opened by its lexical path with no `canonicalFilePath()` / `isInsidePath()` / `isSymLink()` check. Symlinked directories are **not** descended (Qt refuses without `FollowSymlinks`) — this is a file-symlink bypass, not recursive directory traversal.
- **Impact:** A symlink file inside a workspace (planted by a cloned/malicious repo) pointing at `~/.ssh/id_rsa`, `~/.env`, or `/etc/passwd` is read (up to 2 MB) and matching lines are returned to the model.
- **Remediation:** For each file, resolve `QFileInfo(filePath).canonicalFilePath()`, skip it if `isSymLink()` or if the canonical path is not inside the canonical root; also skip non-regular files (FIFOs/devices, whose `open()` can block).

### MB-032 — High, Confirmed in code; exploitability Suspected — Agent: LLM/UI JSON plan accepted verbatim, with no tool allowlist or argument validation

- **Sources:** Part 3 V-20
- **Files:** `app/agent/AgentPlanner.cpp:106-110` (`setPlan`), `:117-125` (`plan()` JSON branch), `:56-76` (`PlanStep::fromJson`); `app/agent/AgentPlanner.h:34-35`
- **CWE:** CWE-20, CWE-94
- **Evidence:** The header states: "An explicit JSON plan is accepted verbatim; otherwise conservative keyword-based planner creates steps. LLM/UI can replace plan at any time via `setPlan()`." `if trimmed.startsWith('[') { doc=fromJson; if doc.isArray() { m_steps.clear(); for value: appendStep(PlanStep::fromJson(value.toObject())); } }` — `toolName`/`args`/`status`/`observation` are taken as-is; `requiresTool` defaults to `!toolName.isEmpty()`; there is no registry lookup. The only bound is `appendStep()` capping at `m_maxSteps` (12).
- **Impact:** Any source that can supply a goal string or call `setPlan()` — in the header's own design, the LLM — can dictate arbitrary tool calls (`run_terminal`, `write_file`, `git_push`, …) with arbitrary arguments. The downstream `ToolRegistry`/`ToolApproval` gate was not verified as a defence here.
- **Remediation:** Validate every step against the registry tool list and each tool's argument schema; reject unknown tools/arguments; drop the verbatim-JSON branch, or require it only from a trusted non-model source with an explicit user plan-approval step. Never feed raw LLM output as a plan.

### MB-033 — High, Confirmed — Agent: a plan executes immediately with no plan approval; the keyword planner turns ordinary words into destructive tools

- **Sources:** Part 3 V-21
- **Files:** `app/agent/AgentOrchestrator.cpp:47-76` (`run()` → `plan()` → `advance()`), `:98`; `app/agent/AgentPlanner.cpp:160-182` (commit/branch/push/pull), `:205-214` (`write_file`), `:227-249` (`run_terminal`)
- **CWE:** CWE-862, CWE-284
- **Evidence:** `const steps = m_planner.plan(m_goal, m_memory.buildContextSummary()); m_memory.addNote("goal: %1"); emit planReady(steps); advance();` — executes the first pending step immediately, with no wait for plan approval. The planner uses substrings: `if lower.contains("run ")||"execute"||"build"||"compile"||"test" { if command.contains(meta) { args["command"]=command; args["shell"]=true; } }`. A single goal can queue `git_commit` + `git_push` + `write_file` + `run_terminal` together; "test"/"commit" also fire on ordinary words.
- **Impact:** A user typing a goal (or a poisoned goal) triggers execution without ever confirming the derived plan. Only the per-tool approval gate (default Prompt) stands between the plan and the effect — and that is bypassable by `AutoApprove` / always-allowed (MB-023).
- **Remediation:** Pause after `planReady` for explicit user approval of the whole plan (show tool + arguments per step); require re-approval if `refine()` appends steps; use word-boundary/intent parsing instead of substring matching; never set `shell:true` from raw goal text.

### MB-034 — High, Confirmed mixing; injection Suspected — Agent: tool output and memory are replayed as undelimited, unlabelled trusted context (prompt injection, latent)

- **Sources:** Part 3 V-22
- **Files:** `app/agent/AgentMemory.cpp:163-186` (`buildContextSummary`), `:182`; `app/agent/AgentOrchestrator.cpp:68-69,151-153,105,253`; `app/agent/AgentPlanner.cpp:131-133`
- **CWE:** CWE-1426, CWE-20
- **Evidence:** `lines << "Project context:"; lines << "  %1: %2"...` verbatim; `lines << "Recent history:"; lines << "  [%1] %2".arg(note.category, note.text.left(300));` — no delimiters, no source/trust label. `m_memory.addNote("%1: %2".arg(step.toolName, observation.left(240)), ok?"observation":"error");` — untrusted tool observation becomes a memory note. `preamble << "Goal: %1".arg(trimmed.left(400)); preamble << "Known context:\n%1".arg(context.left(1200));`. `remember()` / `recall()` / `searchTool()` (`AgentMemory.cpp:393-486,511-560`) store/return content + metadata verbatim with no provenance/trust field.
- **Impact:** File contents, web/tool output, or a poisoned scratchpad/project entry can be persisted and replayed alongside the user goal. The in-scope planner only uses context in a reasoning-step description (it does **not** parse context for tool names/arguments), and no LLM prompt consumer was found — so end-to-end instruction hijack is **not** demonstrated in this snapshot. If/when the summary or recalled memory is fed to an LLM, injected instructions are indistinguishable from the user's.
- **Remediation:** Wrap all recalled/tool/memory content in explicit data-only delimiters with a system instruction that it is data, never instructions; tag each record with source + trust level and surface it; strip/escape delimiter strings; never let memory alone authorise a tool call; keep the MB-032 plan allowlist as a backstop.

### MB-035 — High, Confirmed — Skills: the security scan is advisory only, bypassable, and invisible at enable-time

- **Sources:** Part 3 V-23
- **Files:** `app/skills/SkillsManager.cpp:19,64-122` (`scanPath`), `:124-204` (`installSkill`), `:222-233` (`setEnabled`); `app/skills/SkillsManager.h:53-55`; `app/ui/SkillsHubView.qml` (no warnings UI)
- **CWE:** CWE-693, CWE-20
- **Evidence:** `scanPath` has only 4 patterns, one warning per file, and files over 2 MiB are skipped: `{"curl/wget", "\b(curl|wget)\b"}`, `{"rm -rf", "rm\s+(-rf|-fr)"}`, `{"eval()", "\beval\s*\("}`, `{"base64 decode", "\bbase64\b.*\b(-d|-D|--decode)\b"}` with `break;` — one warning per file, `if fi.size()>2MiB continue;`. `installSkill` scans **after** copying, then installs regardless, even at "error" severity: `warnings=scanPath(target); m_warnings[realId]=warnings; emit skillInstalled(realId,warnings);` — no block, no quarantine. `setEnabled()` does no scan and no warning check at all.
- **Impact:** A skill containing `curl | sh`, `rm -rf`, `eval(`, or base64-decoded payloads installs and can be enabled with one toggle and no visible warning (the QML hub never displays `securityWarnings`). The patterns miss `os.system`/`subprocess`, `powershell -enc`, `Invoke-Expression`, `chmod +x`, extensionless scripts, obfuscated commands, and payloads in files over 2 MiB. There is no signature, hash, or publisher allowlist anywhere in scope.
- **Remediation:** Block installation (or quarantine plus require explicit recorded acknowledgement of each error warning) instead of warning-only; re-scan on enable and surface warnings in the UI; expand detection (execution primitives, network, decoding, privilege); verify publisher signature/hash against an allowlist before installation.

### MB-036 — High, Confirmed in code; exploitability Suspected — SkillsShClient: arbitrary base URL, raw skillId in the URL, token sent to any host, no timeout/size limit, unverified content (latent)

- **Sources:** Part 3 V-24
- **Files:** `app/skills/SkillsShClient.cpp:15` (`setBaseUrl`), `:18-26` (`makeRequest`), `:80-103` (search), `:105-125` (fetchDetails), `:127-161` (fetchSkillContent); `app/skills/SkillsShClient.h:28-34,50`
- **CWE:** CWE-918, CWE-200, CWE-400, CWE-20
- **Evidence:** `setBaseUrl(url)` has no allowlist, `http` is allowed; if the token is non-empty, `req.setRawHeader("Authorization", ("Bearer "+m_token));` is sent to **any** base URL on every request; `url = m_baseUrl + "/api/skills/" + skillId;` and `+ skillId + "/content";` — no percent-encoding. `search()`: `limit` is passed unvalidated; no `setTransferTimeout` anywhere; `reply->readAll()` is unbounded (`:100,120,148`), then `skillContentFinished(skillId,content)` is emitted with no hash/signature check.
- **Impact:** A caller supplying a non-skills.sh base URL (internal `169.254.x.x` / intranet, or `http://`) turns the client into an SSRF/token-exfiltration vector and leaks the Vercel OIDC token in cleartext / to an attacker host. A `skillId` containing `../`, `/`, `?`, `#` can traverse or inject query/fragment. A malicious server can hang the request forever or return an unbounded body (OOM). Fetched `SKILL.md` is emitted as trusted content with no verification. Currently no consumer was found, so this is latent until wired to install/inject.
- **Remediation:** Hard-allowlist `https://skills.sh` (reject anything else, require HTTPS); percent-encode `skillId` as a single path segment (reject `/ ? # ..`); set a transfer timeout of 15–30s and a max body size; send the token only to the allowlisted host and clear/expire it; verify content hash/signature before use.

### MB-037 — High, Confirmed — MCP Registry: URL construction with a raw serverId, arbitrary `setBaseUrl`, API key sent to any host, no timeout/status check

- **Sources:** Part 3 V-25
- **Files:** `app/mcp/MCPRegistry.cpp:87-88,163-170,59,109-115,127-133,137-144`
- **CWE:** CWE-73, CWE-918, CWE-319, CWE-400
- **Evidence:** `url = m_baseUrl + "/v0.1/servers/" + serverId;` (official) and `m_baseUrl + "/servers/" + serverId` (third-party) — `serverId` is **not** percent-encoded (`searchServers` `:69-70` correctly uses `QUrlQuery` for search; the detail endpoint does not). `req.setRawHeader("Authorization", ("Bearer "+m_apiKey));` is sent to whatever base URL was set. `setBaseUrl()` accepts any string including `http://`. There is no `QTimer`/timeout and no HTTP-status check in any registry request (only `reply->error()!=NoError`).
- **Impact:** A `serverId` containing `/`, `../`, `?`, `#` changes the path/query/fragment on the registry host. If a user is tricked into pointing a third-party provider at an attacker host, the BYOK API key is sent there, possibly over plaintext HTTP; a hung registry request never times out.
- **Remediation:** `QUrl::toPercentEncoding(serverId)` / build the path with `QUrl` segments, validate `serverId` against `^[A-Za-z0-9._-]+$`; require `https` for third-party base URLs, validate the base URL in `setBaseUrl()`; add a 10–15s timeout and check `HttpStatusCodeAttribute` (reject ≥400).

### MB-038 — High, Confirmed; Critical if model/QML-reachable — TerminalSession: persistent interactive shell with no sandbox, timeout, or output cap

- **Sources:** Part 3 V-26
- **Files:** `app/tools/TerminalTool.cpp:425-441,562-611,628-629,686-730`
- **CWE:** CWE-78, CWE-250, CWE-400
- **Evidence:** `resolvedCwd = cwd.isEmpty()? QDir::currentPath() : QDir(cwd).absolutePath(); if !isDir error;` — that is the only check. `shell = env.value("SHELL");` (otherwise `/bin/zsh`|`/bin/bash`); if it is not executable, `shell="/bin/sh"`; then `pid=fork(); execve(shellBytes, argv, envp);`. The poll timer (10 ms interval) drains 16 KiB chunks and emits `dataReceived(...)` with no total cap, no timeout, and no session lifetime limit.
- **Impact:** A long-lived shell outside any sandbox root, with an arbitrary cwd and an arbitrary `SHELL` binary, unbounded output flooding the UI/memory, and no auto-termination.
- **Remediation:** Confine cwd to the sandbox root (canonicalised), allowlist the shell binary, add idle/max lifetime limits, cap total output, and kill the whole process group on close (MB-039). Clarify/restrict who may invoke the QML slot.

### MB-039 — High, Confirmed — Timeout / cancel / close kills only the direct child — orphaned descendants persist

- **Sources:** Part 3 V-27
- **Files:** `app/tools/TerminalTool.cpp:209-215` (timeout `kill()`), `:300-313` (`complete()`: `kill()` + `waitForFinished(1000)`), `:334-338` (`cancel()`); `app/tools/CodeRunner.cpp:272-280` (`stopProcess()`: `kill()` + `waitForFinished(500)`); `app/tools/TerminalTool.cpp:597,751-765` (TerminalSession)
- **CWE:** CWE-400
- **Evidence:** No `setpgid`/`killpg`/`kill(-pid,…)`, no process-group/`QProcess` child tracking anywhere. The child does `setsid()`, making it a session leader, yet `close()` does `::kill(pid,SIGHUP); ::kill(pid,SIGKILL);` — a single PID only. Windows: `TerminateProcess(process,1)` only.
- **Impact:** `sh -c 'sleep 1000 & wait'`, `nohup … &`, a daemonising Python/C++ program, or a backgrounded child survives timeout/cancel/session-close, continuing to consume CPU, hold files/ports, and exfiltrate data.
- **Remediation:** Put each child in its own process group (`setpgid` / `CREATE_NEW_PROCESS_GROUP` on Windows) and kill the whole group (`killpg` / terminate-process-tree) on timeout/cancel/close, with a short grace period then `SIGKILL`.

### MB-040 — High, Confirmed — No CPU / memory / disk / process-count limits; unbounded concurrency in TerminalTool

- **Sources:** Part 3 V-28
- **Files:** `app/tools/TerminalTool.cpp:196,208`; `app/tools/CodeRunner.cpp:79,124,206`
- **CWE:** CWE-400, CWE-770
- **Evidence:** The timeout is bounded (`qBound(100,…,600000)` in TerminalTool `:196`; `qBound(1000,…,600000)` in CodeRunner `:79`) and one-shot output is capped (see §12), but there is no `setrlimit`/cgroup, no code-size limit (CodeRunner `:124` `code.toUtf8()` is unbounded), no disk-write limit, and `TerminalTool::m_running` (`TerminalTool.h:58`) has no concurrency cap — unlimited parallel `execute()` calls. CodeRunner mitigates only by serialising one job (CodeRunner `:73` `abortJob()`).
- **Impact:** A fork bomb, memory/CPU hog, or gigabytes written to disk by a snippet/shell command; many parallel terminal calls multiply it. A timeout of up to 10 minutes does not bound the resource rate.
- **Remediation:** `setrlimit` (CPU, `RLIMIT_AS`, `RLIMIT_FSIZE`, `RLIMIT_NPROC`) / cgroups per child, cap snippet size (for example 64–256 KiB), cap concurrent TerminalTool runs, and quota the scratch dir.

### 7.3 Core, storage, models, inference, hardware, chat — High

### MB-041 — High, Confirmed — Unsigned shared-library loading from CWD / app directory (DLL / `.so` planting → arbitrary code execution)

- **Sources:** Part 4 H1
- **Files:** `app/hardware/BackendDetector.cpp:90-103, 107-146, 232-243`
- **CWE:** CWE-426, CWE-427, CWE-829
- **Evidence:** `searchDirectories()` (`:90-103`): `dirs << QDir(appDir).filePath("ggml"); dirs << QDir(appDir).filePath("backends"); dirs << QDir(appDir).filePath("../lib"); dirs << QDir::currentPath();` (`:101` — the CWD is searched). `probeLibrary()` (`:122-123,135-136`): `QLibrary library(fullPath); if (library.load()) { … } library.unload();` and `QLibrary library(candidate);` — a bare name such as `"ggml-cuda"`, resolved via the system search path. No hash, signature, canonical-path, ownership, or allowlist check anywhere in scope.
- **Impact:** Loading a library runs its initialisers/`DllMain` immediately; `unload()` does not undo that. Anyone who can write `libggml-cuda.so` / `ggml-cuda.dll` into the launch directory (Downloads, a shared/project folder, an extracted archive), the app dir, `appDir/ggml`, `appDir/backends`, or `appDir/../lib` gets code execution when hardware detection runs (triggered from QML via `Application::hardwareInfo()`, for example Home/Settings/Onboarding).
- **Remediation:** Do not probe by loading. Resolve with `QFileInfo::canonicalFilePath()`, require the file to be under the signed install directory only (never CWD, never `../lib`, never the bare system search path), and verify a hash/signature against a pinned manifest before any load. Remove `QDir::currentPath()` and the bare-name fallback loop.

### MB-042 — High, Confirmed missing control; exploitability Suspected (needs a runtime test of tool defaults) — Archive extraction with no zip-slip / symlink validation

- **Sources:** Part 4 H2
- **Files:** `app/inference/EngineInstaller.cpp:198-226`
- **CWE:** CWE-22, CWE-59
- **Evidence:** `p.start("tar", {"-xzf", archivePath, "-C", destDir});` — no member allowlist, no `--no-absolute-filenames`. `p.start("unzip", {"-o", archivePath, "-d", destDir});` — `-o` overwrites silently. Windows: `Expand-Archive -LiteralPath '<archive>' -DestinationPath '<dest>' -Force`. No entry-name validation, symlink check, or allowlist of expected files anywhere before extraction.
- **Impact:** A crafted archive (from a compromised release, MB-009) with `../`, absolute-path, or symlink entries can write/overwrite files outside `…/engine/` and plant a malicious `libllama.so`/`llama.dll` or overwrite app files.
- **Remediation:** Extract with a library (for example QuaZip/libarchive) that validates every entry: reject absolute paths, `..`, and symlinks/hardlinks; resolve each destination with `PathSecurity::isInside()` and skip/reject violators; extract to a fresh temp dir and rename into place.

### MB-043 — High (Windows), Confirmed injection sink; trigger Suspected (needs a `'` in a path/asset name) — PowerShell command injection via unescaped single-quoted paths in `Expand-Archive`

- **Sources:** Part 4 H3
- **Files:** `app/inference/EngineInstaller.cpp:200-213`
- **CWE:** CWE-78, CWE-88
- **Evidence:** `args << "-NoProfile" << "-Command" << QString("Expand-Archive -LiteralPath '%1' -DestinationPath '%2' -Force").arg(archivePath, destDir); QProcess p; p.start(cmd, args);` `archivePath` embeds `m_latestAssetName` (from the GitHub API URL, `section('/', -1)`, unsanitised — `:98-101, 154-157`) and `destDir` embeds the user's profile path (`QStandardPaths::AppDataLocation`). A single quote in either breaks out of the quoted string into arbitrary PowerShell. A Windows username containing `'` (for example `O'Brien`) is a plausible non-attacker trigger.
- **Impact:** Arbitrary PowerShell execution as the user during engine install.
- **Remediation:** Do not build a PowerShell command string. Use `QProcess` with an argument list for a native extractor, or pass paths via a script file / base64-encoded `-EncodedCommand`, or use a C++ zip library. Sanitise/validate `m_latestAssetName` against `^[A-Za-z0-9._-]+$` regardless.

### MB-044 — High, Confirmed — Updater signing key is a truncated placeholder; all updates fail (or must be "fixed" unsafely)

- **Sources:** Part 4 H4
- **Files:** `app/core/Updater.cpp:386-393, 302-333, 271-284`; `app/core/Updater.h:40-41`; `CMakeLists.txt:153-157`
- **CWE:** CWE-347, CWE-321
- **Evidence:** `return "-----BEGIN PUBLIC KEY-----\n" "MCowBQYDK2VwAyEA" /* placeholder */ "\n" "-----END PUBLIC KEY-----\n";` This is not a complete Ed25519 SPKI key — `PEM_read_bio_PUBKEY()` at `:310` will fail, so `verifySignature()` returns `false`. Also, `verifySignature()` (`:317`) passes `EVP_sha256()` to `EVP_DigestVerifyInit`, but Ed25519 requires a NULL digest — incorrect for Ed25519 even with a real key (needs runtime confirmation). `onDownloadFinished()` (`:278-284`) correctly fails closed on a missing/invalid signature, so this is **not currently bypassable**, but the updater is non-functional as shipped. `CMakeLists.txt:153-157` also makes OpenSSL optional: without it `verifySignature()` returns `false` (`:329-333`, fail-closed).
- **Impact:** No update can ever install; the pressure to disable the signature check or ship an ad-hoc key is the real danger. If a real key is pasted incorrectly, the same brick results.
- **Remediation:** Generate a real Ed25519 keypair offline, embed the full public key, fix the digest call for the key type, add a CI test that verifies a known-good signed artefact, and fail the build if the placeholder string is present. Keep the fail-closed behaviour.

### MB-045 — High, Confirmed pattern; exploitation Suspected (needs a local attacker / runtime check) — Updater download path is predictable and symlink-followed; a local attacker can overwrite arbitrary files before verification

- **Sources:** Part 4 H5
- **Files:** `app/core/Updater.cpp:196-199, 233-241`
- **CWE:** CWE-59, CWE-367, CWE-377
- **Evidence:** `const QString base = QStandardPaths::writableLocation(QStandardPaths::TempLocation); QDir().mkpath(base + "/ModelBench-updates"); const QString fileName = QUrl(m_downloadUrl).fileName(); m_downloadPath = base + "/ModelBench-updates/" + (fileName.isEmpty() ? "update.bin" : fileName); QFile f(m_downloadPath); if (!f.open(QIODevice::WriteOnly)) { … } f.write(reply->readAll());` No `O_EXCL`/exclusive create, no symlink/`isInside()` check, no restrictive permissions, no random suffix. An attacker pre-creating `/tmp/ModelBench-updates/<predictable-asset-name>` as a symlink to, for example, `~/.bashrc` gets it truncated/overwritten with download bytes on `open(WriteOnly)`. Verification (`:246-284`) happens *after* the write, and `QFile::remove()` on failure removes only the link, not the already-damaged target.
- **Impact:** Local privilege/file-integrity escalation on multi-user systems.
- **Remediation:** Create the temp dir with `0700`, use a random, non-predictable filename (for example `QTemporaryFile` with `O_EXCL`), `lstat()`/reject symlinks, verify before any rename into place, and never write through a pre-existing path.

### MB-046 — High, Confirmed — `installUpdate()` executes the downloaded file with no re-verification and no containment (TOCTOU)

- **Sources:** Part 4 H6
- **Files:** `app/core/Updater.cpp:336-362`
- **CWE:** CWE-367, CWE-829
- **Evidence:** `void Updater::installUpdate() { if (m_state != Ready || m_downloadPath.isEmpty()) { … return; } // … no hash/signature re-check, no isInside(), no extension allowlist … launched = QProcess::startDetached(path, {}); // Linux :361; launched = QProcess::startDetached(path, {}); // Windows non-.msi :350; launched = QProcess::startDetached("open", {path}); // macOS :354-358 }` Verification in `onDownloadFinished()` is a one-time check; the user may click Install much later. A local attacker swapping/replacing the file (or its symlink, see MB-045) between `Ready` and `installUpdate()` gets arbitrary code execution as the user. There is no check that `path` is still inside `…/ModelBench-updates/`, is a regular file, or has an expected extension (Linux executes any file regardless of extension).
- **Impact:** Local attacker → code execution in user context.
- **Remediation:** Re-verify SHA-256 + signature immediately before launch (re-read by fd/inode, not by path), reject symlinks, require the file to be inside the updater temp dir, allowlist extensions per OS (`.msi`/`.exe`, `.dmg`/`.pkg`, distro package), and launch with an absolute, validated path.

### MB-047 — High, Confirmed — Credentials sent over plaintext HTTP; no HTTPS enforcement

- **Sources:** Part 4 H7; Part 1 F-23 (merged — QML layer of the same issue)
- **Files:** `app/core/AccountManager.h:40`, `app/core/AccountManager.cpp:14-22, 35-36, 49-50, 74-75`; `app/ui/LoginView.qml:97-98, 105-108`
- **CWE:** CWE-319, CWE-523
- **Evidence:** `QString m_serverUrl = "http://localhost:3000";` `setServerUrl()` (`QML-writable`, `Q_PROPERTY … WRITE setServerUrl`, exposed in `LoginView.qml:97-98` as free text) accepts any scheme/host with no validation: `void AccountManager::setServerUrl(const QString &url) { if (m_serverUrl == url) return; m_serverUrl = url; }` then `QNetworkRequest req{QUrl(m_serverUrl + path)};` (`postJson()`), `postJson("/auth/login", QJsonObject{{"email", email}, {"password", password}}, …)`, `req.setRawHeader("Authorization", ("Bearer " + m_accessToken).toUtf8());`. Unlike `Updater::isHttps()`, there is no HTTPS check. The QML layer likewise allows any server URL including plain HTTP, with no validation (`LoginView.qml:105-108`). The default is `http://`, so a user typing `http://<host>` (or a typo / attacker-supplied URL) sends email + password and the Bearer token in cleartext.
- **Impact:** Credential/token interception on any non-loopback HTTP network; account takeover.
- **Remediation:** Enforce `https://` except loopback (`127.0.0.1`/`::1`/`localhost`) in `setServerUrl()`/`postJson()`; reject other schemes; change the default to `https://`; warn in the UI for HTTP. Do not rely on Qt defaults alone. Require https in the QML layer too, allowing `http://localhost` only for development.

### MB-048 — High, Confirmed — Model downloads have no integrity, size, or resume verification; corrupt/truncated files are reported as success

- **Sources:** Part 4 H8
- **Files:** `app/models/ModelDownloader.cpp:25-58`; `app/models/HuggingFaceClient.cpp:59-82`
- **CWE:** CWE-494, CWE-353, CWE-390
- **Evidence:** Resume trusts the local file blindly: `if (fi.exists()) existing = fi.size(); if (existing > 0) req.setRawHeader("Range", ("bytes=" + QString::number(existing) + "-").toUtf8());` The write result is unchecked and there is no status/size check: `if (m_file.isOpen()) m_file.write(m_reply->readAll()); if (m_reply->error() != NoError && m_reply->error() != OperationCanceledError) emit failed(...); else emit finished(destPath);` The server is never checked for `206` vs `200` (a server ignoring `Range` gets its full body **appended** to the partial file, corrupting it), `Content-Length`/total is never compared, `QFile::write()`'s return is ignored, and a failed download leaves the partial file in place for the next resume. `HuggingFaceClient::fetchModelFiles()` also discards everything except `path`/`size` — it never surfaces the HF `sha256`/`oid` that would allow verification.
- **Impact:** Truncated, duplicated, or tampered `.gguf` files are presented as downloaded and then loaded by `llama.cpp` (MB-051). A network/MITM fault silently produces a corrupt model.
- **Remediation:** Capture `sha256` from the HF API, verify the completed file's SHA-256 (and expected size) before emitting `finished()`; verify `206` before resuming, otherwise restart from 0; check `write()` bytes; download to `*.part` and rename only on success; delete partial files on failure.

### MB-049 — High, Confirmed — Cancelling a download emits `finished()` — a partial file is treated as complete

- **Sources:** Part 4 H9
- **Files:** `app/models/ModelDownloader.cpp:47-65`
- **CWE:** CWE-390, CWE-754
- **Evidence:** The finished handler (`:49-53`): `if (m_reply->error() != QNetworkReply::NoError && m_reply->error() != QNetworkReply::OperationCanceledError) emit failed(...); else emit finished(destPath);` — `OperationCanceledError` falls through to success. `cancel()` (`:61-65`): `if (m_reply) m_reply->abort(); if (m_file.isOpen()) m_file.close(); m_running = false;`
- **Impact:** The UI marks a cancelled/partial download as done; the partial `.gguf` is then offered for loading (combined with MB-048, no size/hash check ever catches it).
- **Remediation:** Emit a distinct `cancelled` signal (or `failed("Cancelled")`) on `OperationCanceledError`; delete/keep-as-`.part` the partial file; never emit `finished()` unless the transfer completed with matching size/hash.

### MB-050 — High, Confirmed — `LlamaBackend` has no worker thread and no locking, despite the header promising both — concurrent load/generate/unload is a use-after-free / double-free race

- **Sources:** Part 4 H10
- **Files:** `app/inference/LlamaBackend.cpp:44-48, 49-100, 103-180`; `app/inference/LlamaBackend.h:66-74, 104-112`
- **CWE:** CWE-362, CWE-416, CWE-662
- **Evidence:** The header (`:66-74`) documents: "All llama.cpp calls run on a dedicated worker thread... safe to call from the GUI thread." The implementation (`:44-48`) is a synchronous no-op: `bool LlamaBackend::invokeWorker(std::function<void()> fn, bool blocking) const { Q_UNUSED(blocking); fn(); return true; }` `grep` confirms `m_thread`/`m_threadContext` are never assigned, `moveToThread` is never called, and there is **no mutex anywhere** in `app/models` or `app/inference`. `loadModel()` (`:49`, calls `unloadModel()` then frees/replaces `e.model`/`e.ctx`/`e.sampler`), `generate()` (`:103-180`, a long blocking loop touching `e.ctx`/`e.sampler`), `unloadModel()` (`:89-100`), `cancel()`, and `~LlamaBackend()` (unload) all touch the same unsynchronised `Engine`. All three operations are reachable from QML (`loadModelFile`, `generateText`, `unloadModelQml`, `cancelGeneration` — `LlamaBackend.h:122-130`).
- **Impact:** Concurrent load + generate, generate + unload, or destruction during generation → use-after-free / double-free / crash, and potentially exploitable memory corruption with a crafted model. `generate()` also blocks the calling (GUI) thread for the whole generation — UI freeze / DoS.
- **Remediation:** Implement the documented design: move the backend to a real `QThread` (or serialise all engine access behind a `QMutex`/strand), make `loadModel`/`generate`/`unloadModel` mutually exclusive, and join/cancel generation in the destructor before freeing the engine.

### MB-051 — High, Confirmed — Arbitrary GGUF deserialisation: `loadModelFile()` loads any path, with no containment, extension, or symlink check

- **Sources:** Part 4 H11
- **Files:** `app/inference/LlamaBackend.cpp:49-57`; `app/inference/LlamaBackend.h:122`; `app/models/ModelDownloads.cpp:8-14`
- **CWE:** CWE-434, CWE-59, CWE-200
- **Evidence:** `Q_INVOKABLE bool loadModelFile(const QString &path)` → `loadModel(path, cfg)`; `e.model = llama_model_load_from_file(path.toUtf8().constData(), mparams);` (`:53`); `if (!e.model) { emit generationError("Failed to load model: " + path); return false; }` No existence check, no `.gguf` check, no containment under the models directory, symlinks followed. `listDownloadedModels()` lists `*.gguf` by name only, so a symlinked `.gguf` pointing outside the models dir is listed and loadable. The error also leaks the full path to the UI.
- **Reviewer disagreement:** One reviewer rated this Medium; the synthesis rated it High, because the file is parsed by native code and parser safety itself is unverified (see §16). This document rates it **High**.
- **Impact:** A downloaded/corrupt/malicious GGUF (MB-048 — no hash check) is parsed by the complex `llama.cpp` GGUF parser; parser memory-safety bugs are a known remote-code-execution vector for model files. The path leak is minor.
- **Remediation:** Require the path to resolve (canonical, symlink-resolved) inside `Paths::modelsDirectory()` with a `.gguf` extension; verify size/hash before load; do not include the raw path in error strings shown to the UI; keep `llama.cpp` patched.

### MB-052 — High, Confirmed — `SecretStorage`: path traversal via unsanitised `service` / `account`

- **Sources:** Part 4 H12; Part 1 noted BUG-207 (SecretStorage path traversal) as still open in `todo.md:67` and not verifiable from the build/CI scope
- **Files:** `app/storage/SecretStorage.cpp:28, 42, 59`
- **CWE:** CWE-22, CWE-73
- **Evidence:** `store()` (`:28`): `QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/secrets/" + service + "_" + account + ".bin";` `load()` (`:42`) uses the same construction; `remove()` (`:59`) the same, returning `QFile::remove(path);`. No validation, allowlist, `cleanPath()`, or containment check anywhere in the file (`grep` for `isSymLink|canonical|cleanPath` in scope: none).
- **Impact:** Any caller passing `service`/`account` containing `../`, `/`, or `\` escapes `secrets/`. `store()` writes an attacker-chosen DPAPI blob to an arbitrary `*.bin` path, `load()` reads an arbitrary `*.bin` file for DPAPI decryption, and `remove()` deletes an arbitrary `*.bin` file. Note `remove()` is *not* behind `#ifdef Q_OS_WIN`, so it is active on all platforms.
- **Remediation:** Reject/allowlist `service`/`account` to `[A-Za-z0-9_-]+` (or hash them: `SHA256(service + '\0' + account)` as the filename), build the path with `QDir`, and assert `QDir::cleanPath(path).startsWith(secretsDir + '/')` before any I/O.

### MB-053 — High, Confirmed ingestion/storage; Suspected remote exfiltration — `CodeIndexer` indexes secrets, stores them in plaintext, and sends them to the embedding provider

- **Sources:** Part 4 H13
- **Files:** `app/storage/CodeIndexer.cpp:51-62, 65-73, 340-360`
- **CWE:** CWE-200, CWE-312
- **Evidence:** `defaultExtensions()` includes `json,yaml,yml,toml,txt,md,sql,sh,ps1` (`:51-62`); `defaultExcludeDirs()` (`:65-73`) excludes `.git,build,node_modules` etc. but has **no** denylist for `.env`, `*secret*`, `*credential*`, `*.pem`, `*.key`, `.aws/`, etc., and there is no filename/content secret scan. Then: `e.metadata = {{"file_path", c.filePath}, {"absolute_path", c.absolutePath}, ...}; if (m_embeddingProvider) e.embedding = m_embeddingProvider(e.content); else if (m_store->embeddingProvider()) e.embedding = m_store->embeddingProvider()(e.content); if (m_store->upsert(e)) …`
- **Impact:** A project containing `credentials.json`, `config.yaml`, `secrets.txt`, or `.env` (if given an indexed suffix) is chunked verbatim into the plaintext SQLite `vector_entries` table (see MB-105) and its full content is handed to the embedding provider. If that provider is a remote API, secrets leave the machine; even locally they become searchable via `search()`. Whether the provider is remote was not verified, hence Suspected for exfiltration.
- **Remediation:** Add a sensitive-filename/extension denylist (`.env*`, `*.pem`, `*.key`, `*credentials*`, `*secret*`, `.aws/`, `.ssh/`), skip files whose content matches high-entropy/known secret patterns, never store `absolute_path` in metadata, and require explicit opt-in before sending indexed content to a remote embedder.

### 7.4 Build, CI, release, signing, docs — High

### MB-054 — High, Confirmed — Release publishes unsigned artefacts by default; no checksums, SBOM, or provenance

- **Sources:** Part 1 F-01
- **Files:** `.github/workflows/release.yml:53-58, 91-96, 108-116`
- **CWE:** CWE-347, CWE-494
- **Evidence:** Signing is optional (`if: secrets.WINDOWS_CERT_PFX != ''`, `MACOS_SIGN_IDENTITY`), `softprops/action-gh-release@v2` with `files: artifacts/**/*`, and there is no SHA256SUMS / `.sig` / SBOM / attestation.
- **Impact:** Users cannot verify that a downloaded release is the one the project built, or that it has not been tampered with. Combined with MB-009 and MB-044, the whole release-to-install chain is unverified.
- **Remediation:** Mandatory signing, SHA256SUMS plus detached signatures, provenance attestation, and checksum verification before publish.

### MB-055 — High, Confirmed — Update-manifest / Ed25519 signing is documented but has no implementation or release step

- **Sources:** Part 1 F-02
- **Files:** `docs/RELEASE.md:20-27`, `scripts/release/update-manifest.template.json`, `scripts/signing/signing-config.template.json:15-19`; `release.yml` has no manifest step
- **CWE:** CWE-347, CWE-494
- **Evidence:** The template contains only placeholders. There is no manifest-generation or signing step in the release workflow.
- **Impact:** The updater (MB-044, MB-097) has no signed manifest to consume; the documented signing flow does not exist in practice, so any future "fix" is likely to be ad-hoc.
- **Remediation:** Real manifest generation plus Ed25519 signing in `release.yml`, publish the manifest, and test that the updater rejects a bad/missing signature.

### MB-056 — High, Confirmed — `tests/security/*.js` are non-runnable stubs; they test nothing

- **Sources:** Part 1 F-03
- **Files:** `tests/security/*.js`
- **CWE:** CWE-1059
- **Evidence:** The files import non-existent `../app/core/PathSecurity.js`, `../app/mcp/MCPClient.js`, `../app/storage/SecretStorage.js`, etc. (`.js` versions of C++ files that do not exist); the bodies are `expect(true).toBe(true)` placeholders. Only `test_credits_flow.js` is registered in CTest, and only if node exists.
- **Impact:** The repository presents security tests that provide no assurance at all. A reader (or CI badge) can reasonably conclude the security properties are tested when they are not.
- **Remediation:** Rewrite as real tests against the C++ code via QtTest, or against the real server routes via supertest; register them in CTest/CI; remove the placeholders.

### MB-057 — High, Confirmed as documented, Suspected as still-present — `docs/SECURITY.md` self-reports 7 known issues

- **Sources:** Part 1 F-05
- **Files:** `docs/SECURITY.md:11-50`
- **CWE:** CWE-347, CWE-922, CWE-942, CWE-307, CWE-200
- **Evidence:** The document self-reports: (1) webhook signature not implemented; (2) no cross-platform secret storage, macOS/Linux store/load return `false`/`{}`; (3) CORS allows all origins with credentials (`cb(null, true)`); (4) auth token held in RAM only; (5) MCP approval gate is async/UI-dependent, with no timeout/auto-deny; (6) no rate limiting on auth/webhook; (7) secret arguments in plaintext to the approval UI preview. Each item maps to a specific finding in this document: (1) MB-003, (2) MB-107, (3) MB-016, (4) MB-108 / MB-062 (token handling), (5) MB-073 / MB-080, (6) MB-061, (7) MB-025. The document is accurate as a list; whether each item is still present in code was Suspected at the docs-scope level and is Confirmed for the items cross-referenced here.
- **Impact:** The project knowingly ships with these issues documented but unfixed, and the document gives no timeline, owner, or severity for any of them.
- **Remediation:** Fix the items in the order listed in §14, and remove each item from `docs/SECURITY.md` only when a real test proves it fixed.

### MB-058 — High, Confirmed — ChatView approval UI is a local stub, not wired to the real orchestrator

- **Sources:** Part 1 F-06
- **Files:** `app/ui/ChatView.qml:51-63, 68-90`
- **CWE:** CWE-862
- **Evidence:** `startAgentFlow` builds fake steps; `approveAgentStep` / `denyAgentStep` only mutate a local JS array; there is a TODO to wire it to the real orchestrator.
- **Impact:** A user who clicks Approve/Deny believes they have controlled an agent action, when nothing has been approved or denied. This is a fail-open signalling flaw in the very UI that is supposed to be the human control.
- **Remediation:** Wire it to the real signals, or remove Approve/Deny until wired; add a UI test.

---

## 8. Findings — Medium

### 8.1 Server / billing — Medium

### MB-059 — Medium, Confirmed — Internal error messages are returned verbatim to clients

- **Sources:** Server M1
- **Files:** `server/src/app.js:55-75` (error middleware), `server/src/lib/credits.js:250-270` (`mapCreditError`), `server/src/lib/openrouter.js:45-55`, `server/src/lib/whop.js:20-35`
- **CWE:** CWE-209, CWE-215
- **Evidence:** A non-`AppError` with status <500 returns `error.message`; credits wraps a raw Supabase/RPC error as `AppError` 500 `credits_error`; openrouter/whop throw `AppError` 502 with upstream text sliced to 400/500 chars; Supabase errors are wrapped as `profile_error` with `upsertError.message`.
- **Impact:** Database, upstream-provider, and internal error detail leaks to clients, aiding an attacker in mapping the system.
- **Remediation:** Return a generic message plus a server-side correlation ID for all 5xx errors, log the full error server-side only, and never forward upstream bodies.

### MB-060 — Medium, Confirmed — No security headers at all

- **Sources:** Server M2
- **Files:** `server/src/app.js:10-20` (only `app.disable('x-powered-by')`), `server/src/routes/billing.js:152-165` (`/billing/mock-success` returns HTML)
- **CWE:** CWE-693, CWE-1021, CWE-319
- **Evidence:** No helmet, CSP, HSTS, nosniff, frame-options, or Referrer-Policy anywhere.
- **Impact:** The API and its HTML responses have none of the standard browser protections; clickjacking, MIME-sniffing, and downgrade attacks are unmitigated.
- **Remediation:** Add helmet or explicit headers, a restrictive CSP, nosniff, `frame-ancestors 'none'`, HSTS behind HTTPS, and `Referrer-Policy: no-referrer`; add the same headers to the DEPLOY.md Nginx example.

### MB-061 — Medium, Confirmed — Rate limiting is per-IP only, in-memory, unbounded, and silently disabled by bad configuration

- **Sources:** Server M3
- **Files:** `server/src/lib/rateLimit.js:1-80`, `server/src/routes/auth.js:10-25`, `server/src/routes/ai.js:20-35`, `server/src/routes/billing.js:15-25`
- **CWE:** CWE-770, CWE-307, CWE-400
- **Evidence:** `buckets = new Map()` is never capped; the sweep only deletes expired entries ≥60s apart. `clientKey = scope:req.ip`. `max: Number(process.env.AI_RATE_LIMIT_MAX || 60)` — `Number('abc') === NaN`, so `bucket.count > NaN` is always false and there is **no limiting at all**. `disabled()` returns true if `RATE_LIMIT_DISABLED === 'true'`. Unprotected routes include GET `/auth/me`, POST `/auth/logout`, all `/user/*`, GET `/ai/usage`, GET `/billing/entitlements`, GET `/billing/plans`, GET `/health`, and GET `/billing/mock-success`. Limiting is per-IP only, with no per-account limiting and no lockout.
- **Impact:** A typo in an env var silently disables rate limiting; brute-force and abuse are unthrottled on the unprotected routes.
- **Remediation:** Validate env limits at startup (reject non-finite / ≤0, fail closed), cap the bucket store (LRU/max), add per-account limiting on login/signup, a global limiter, limiters on the unlimited routes, Redis for multi-instance deployments, and guard `RATE_LIMIT_DISABLED` in production.

### MB-062 — Medium (mock only; Critical if mock is exposed — see MB-001), Confirmed — Mock passwords stored in plaintext; mock user IDs predictable; mock tokens live 30 days

- **Sources:** Server M4
- **Files:** `server/src/lib/mock.js:85-130` (signup/login), `:35-45` (`userIdForEmail`), `:55-75` (`issueToken`/`tokenTtlMs`)
- **CWE:** CWE-256, CWE-257, CWE-338, CWE-613
- **Evidence:** `user = {id: userIdForEmail(email), email, password, …}` — the password is stored in plaintext in a Map, and `user.password !== password` is not timing-safe. `userIdForEmail = mock_${sha256(normalizeEmail(email)).slice(0,24)}` — deterministic from the email. Token TTL defaults to 30 days (`MOCK_TOKEN_TTL_SECONDS || 60*60*24*30`). The tokens themselves are good: `crypto.randomBytes(32)` base64url. The tokens Map and `webhookEvents` Set are never swept (expired tokens are deleted lazily on use).
- **Impact:** In mock mode, anyone who can read the process (or a dump of it) has every password; any email's user ID is computable, making enumeration trivial (MB-011).
- **Remediation:** Hash mock passwords with scrypt/argon2 even in dev, compare with `timingSafeEqual`, use random UUIDs for mock IDs, shorten the TTL (for example 24h), add refresh/rotation, and sweep expired tokens.

### MB-063 — Medium, Confirmed — Live-mode logout does not invalidate the token

- **Sources:** Server M5
- **Files:** `server/src/routes/auth.js:75-85` (POST `/auth/logout` revokes the mock session only; Supabase tokens are client-side)
- **CWE:** CWE-613
- **Impact:** A stolen Supabase JWT remains usable until its natural expiry after logout.
- **Remediation:** Call Supabase Admin `signOut` / revoke the refresh token server-side, document the JWT lifetime, and keep it short (Supabase default is 1h — verify in the dashboard, not verifiable from the repo).

### MB-064 — Medium, Confirmed — Open redirect via `safeRedirect` in checkout (any `https:` URL is accepted)

- **Sources:** Server M6
- **Files:** `server/src/routes/billing.js:60-80, 270-280`
- **CWE:** CWE-601
- **Evidence:** `redirect_url: safeRedirect(body.successUrl) || ${siteUrl()}/billing/success`. `safeRedirect` only checks the protocol (`/^https?:$/`); the host is unrestricted, `body.successUrl` is caller-controlled, and checkout is unauthenticated (MB-012).
- **Impact:** An attacker can redirect a victim after checkout to any `https:` site — a phishing vector on a payment flow.
- **Remediation:** Allowlist redirect hosts (exactly `SITE_URL` plus configured origins), reject anything else, and never accept a redirect URL from an unauthenticated caller.

### MB-065 — Medium, Confirmed — Legacy Whop webhook HMAC has no replay protection

- **Sources:** Server M7
- **Files:** `server/src/lib/whop.js:95-160` (`verifyWebhookSignature` legacy branch), `server/src/routes/billing.js:180-200` (eventId derivation)
- **CWE:** CWE-294, CWE-347
- **Evidence:** The Standard Webhooks scheme is well done (300s timestamp tolerance, webhook-id in the signed payload, `timingSafeEqual`). The legacy branch is not: `digest = Hmac sha256(key).update(body)` — body only, no timestamp, no webhook-id. `eventId = headers['webhook-id'] || event.id || … || sha256(raw)`, so replaying the same body with a different `webhook-id` produces a new eventId and bypasses the `webhook_events` idempotency. The legacy scheme accepts hex and base64.
- **Impact:** A captured legacy webhook can be replayed indefinitely, granting credits repeatedly (MB-017).
- **Remediation:** Deprecate/remove the legacy scheme and require Standard Webhooks. If legacy must stay, enforce a timestamp and record the body hash as the idempotency key.

### MB-066 — Medium, Confirmed in code; Supabase email confirmation Suspected / not verifiable — No entitlement/tier check on cloud AI; no email-verification gate

- **Sources:** Server M8
- **Files:** `server/src/routes/ai.js:110-130` (POST `/ai/chat` only checks balance, never `getEntitlements`/`featuresForTier`, never checks the `cloud_ai` feature that Hobby/Scale/Teams grant and Free does not); `server/src/routes/auth.js:35-70` (signup/login return a session immediately; the server never checks `email_confirmed_at` before creating a profile or serving AI)
- **CWE:** CWE-862, CWE-287
- **Impact:** Any account with a positive balance (mock starting credits, a webhook grant, or leftover credits after cancellation — `deactivatePlan` preserves them) can use cloud AI regardless of tier, subscription, or email verification.
- **Remediation:** Enforce the `cloud_ai` entitlement plus an active subscription on `/ai/chat`, and require a verified email before first paid/cloud use.

### MB-067 — Medium, Confirmed in code; upstream SSRF/prompt-injection impact Suspected — Unvalidated passthrough to OpenRouter: user-controlled system/developer messages, tools, image URLs

- **Sources:** Server M9
- **Files:** `server/src/routes/ai.js:25-35` (`PASS_THROUGH_PARAMS`: `temperature`, `top_p`, `top_k`, `presence_penalty`, `frequency_penalty`, `seed`, `stop`, `response_format`, `tools`, `tool_choice`, `parallel_tool_calls`, `reasoning`), `:185-215` (`validateMessages`); `server/src/lib/openrouter.js:20-45`
- **CWE:** CWE-74, CWE-918, CWE-20
- **Evidence:** There is no server-side system prompt. Any authenticated user can inject system/developer messages. Content parts are forwarded verbatim, including `image_url` for the vision model (`nvidia/nemotron-super-vl`) — OpenRouter will fetch the URL server-side. `tools`/`tool_choice`/`response_format` are forwarded without schema/size limits.
- **Impact:** Prompt injection against any downstream consumer of the conversation, and server-side fetching of attacker-chosen image URLs (SSRF-adjacent, via OpenRouter).
- **Remediation:** Strip/restrict system/developer messages to a server-owned prompt, validate content parts (allow only text, or allowlist `image_url` hosts/schemes and reject private/reserved IPs), and validate/size-limit `tools`, `response_format`, and numeric parameters.

### MB-068 — Medium, Suspected — Email lookup uses an unescaped `ilike`, so `%` / `_` in a webhook email act as wildcards

- **Sources:** Server M10
- **Files:** `server/src/lib/entitlements.js:235-260` (`resolveWebhookUser`: `.ilike('email', normalized.email).limit(1).maybeSingle()`)
- **CWE:** CWE-89, CWE-20
- **Evidence:** PostgREST `ilike` treats `%` and `_` as wildcards, with no escaping. `normalized.email` comes from the webhook payload (attacker-controlled if unsigned/forged — MB-003 — or Whop-metadata attacker-influenced).
- **Impact:** An email such as `%` or `%@example.com` could match the first profile and grant/activate/cancel that user's plan.
- **Remediation:** Escape `%`, `_`, `\` before `ilike`, or use an exact case-insensitive match (`.eq` after lower-casing, or a `lower(email)=lower($1)` RPC). Prefer resolving webhooks by verified `user_id` UUID only.

### MB-069 — Medium, Confirmed — `TRUST_PROXY=1` in the example env plus `req.ip`-based rate limiting = `X-Forwarded-For` spoofing bypass

- **Sources:** Server M11
- **Files:** `server/.env.example:10-15` (`TRUST_PROXY=1`), `server/src/app.js:15` (`app.set('trust proxy', Number(process.env.TRUST_PROXY || 0))`), `server/src/lib/rateLimit.js:15-25` (`clientKey`)
- **CWE:** CWE-290, CWE-307
- **Evidence:** Rate limiting keys by `req.ip`, which is safe only when the proxy is trusted. `.env.example` sets `TRUST_PROXY=1` unconditionally, and DEPLOY.md says to set it if behind a reverse proxy without saying the proxy must strip/overwrite `X-Forwarded-For`. If the server is ever reachable directly (MB-132 binds `0.0.0.0`), an attacker can set `X-Forwarded-For: <fresh IP>` per request and get a fresh bucket each time. `Number('true')` is `NaN` — the env value `'true'` does not enable trust proxy as an operator might expect.
- **Impact:** Rate limiting (MB-061) can be bypassed entirely by rotating `X-Forwarded-For`.
- **Remediation:** Default `TRUST_PROXY=0`, enable it only with a named trusted reverse proxy that strips client `X-Forwarded-For`, validate the value at startup, and consider keying auth limits by account/email as well as IP.

### 8.2 Agent, tools, MCP, skills — Medium

### MB-070 — Medium, Confirmed — CodeRunner: arbitrary cwd escapes the temp dir

- **Sources:** Part 3 V-29
- **Files:** `app/tools/CodeRunner.cpp:71-72,226`
- **CWE:** CWE-22, CWE-250
- **Evidence:** `run(language,code,cwd,…)` → `startRun()`: `process->setWorkingDirectory(job->cwd.isEmpty()? job->dir->path() : job->cwd);` — no validation, no `isInsideRoot`/`PathSecurity::isInside` check, no existence check. The `QTemporaryDir` (`:111`) only holds the source; the snippet's relative filesystem access follows the caller-chosen cwd, and absolute paths are unrestricted anyway.
- **Impact:** A snippet runs with an attacker-chosen working directory, so relative-path access escapes the temp dir.
- **Remediation:** Ignore/validate cwd: force it to the temp dir, or canonicalise it and require it inside an allowlisted root.

### MB-071 — Medium, Suspected — Interpreter/compiler resolved via the inherited PATH

- **Sources:** Part 3 V-30
- **Files:** `app/tools/CodeRunner.cpp:34-42,60-70,94-95`; `app/tools/TerminalTool.cpp:153-155`
- **CWE:** CWE-426, CWE-427
- **Evidence:** `findFirst({"g++","clang++","c++"})` / `{"python3","python"}` / `{"node","nodejs"}` via `QStandardPaths::findExecutable()` — the first match on the parent's PATH, with no pinned absolute path or hash check. Suspected because PATH writability was not verified.
- **Impact:** If PATH is poisoned (MB-019, or a writable directory early in PATH), a trojaned `python3`/`g++` runs instead.
- **Remediation:** Pin absolute interpreter/compiler paths from trusted config and verify them; do not honour caller-controlled PATH.

### MB-072 — Medium, Confirmed — TerminalSession (Windows): the SHELL string is parsed as a full command line; the env block is unvalidated

- **Sources:** Part 3 V-31
- **Files:** `app/tools/TerminalTool.cpp:401-410,515-522`
- **CWE:** CWE-78, CWE-88
- **Evidence:** `shell = env.value("SHELL");` (otherwise `"cmd.exe"`); `commandLine = shell.toStdWString(); CreateProcessW(nullptr, commandLine.data(), …)` — the string is parsed as program + arguments. `environmentBlock()` (`:401-410`) does no `=`/NUL validation on keys (contrast TerminalTool `:185-188`). POSIX is safer (`:562-570` checks executability, `execve(path,{path},envp)` `:610`), but still allows any executable as the shell.
- **Impact:** A crafted `SHELL` value is parsed as a command line, and unvalidated env keys can inject environment entries.
- **Remediation:** Allowlist the shell executable, pass it as `lpApplicationName` with no extra arguments, and validate env keys/values as in `TerminalTool::execute`.

### MB-073 — Medium, Confirmed — MCP: the approval gate is bypassable by design and has no live UI consumer

- **Sources:** Part 3 V-32
- **Files:** `app/mcp/MCPClient.cpp:665-701` (`callTool` → `toolCallRequested` → `approveToolCall`), `:712-740` (`sendRequest`); `app/mcp/MCPClient.h:108-109,113-115`
- **CWE:** CWE-862, CWE-307
- **Evidence:** `qint64 sendRequest(method, params={}, timeoutMs=-1, toolName={});` is public, with no approval check. Any holder of an `MCPClient*` (`MCPServerManager::client()`, `MCPServerManager.h:60`) can call `sendRequest("tools/call",…)` directly, skipping `callTool()`. Approval IDs are sequential: `qint64 id = m_approvalId++;` starting at 1 (`MCPClient.h:256-257`), and `m_pendingApprovals` never expires. A repo-wide grep for `toolCallRequested|approveToolCall` outside `app/mcp/` finds **no** UI/QML listener — so today the gate, if used, would wait forever (no approval path); the only enforcement is caller discipline.
- **Impact:** The MCP approval gate is not a control in practice: it can be bypassed by calling the public method, and it has no consumer to approve anything.
- **Remediation:** Make `sendRequest()` private (or add an internal-only `sendRequestUnchecked` and route `tools/call`/`resources/read`/`prompts/get` through the approval map); add an approval TTL (for example 5 minutes) and purge on disconnect; use random 64-bit approval IDs; wire `toolCallRequested` to a real UI prompt and add a test that `sendRequest("tools/call")` without approval is refused.

### MB-074 — Medium, Confirmed — MCP: unlimited auto-reconnect respawns a (possibly malicious/crashing) server process forever

- **Sources:** Part 3 V-33
- **Files:** `app/mcp/MCPClient.h:223,226` (`m_autoReconnect=true; m_maxReconnectAttempts=-1;` — `<0` = unlimited); `app/mcp/MCPClient.cpp:814-843` (`scheduleReconnect`, exponential backoff capped at 30s); `app/mcp/MCPServerManager.h:27` (`autoReconnect=true` by default)
- **CWE:** CWE-400
- **Evidence:** `scheduleReconnect()` returns early only when `max>=0 && attempts>=max` — with the default `-1` it never fires. `onProcessFinished` / `onSseFinished` / `onHttpPostFinished` all call it.
- **Impact:** A crashing, fork-bombing, or network-flapping MCP server is respawned indefinitely (spawn + handshake + `tools/list` each cycle), burning CPU/network and re-executing attacker code.
- **Remediation:** Default `maxReconnectAttempts` to a small bound (for example 5) and require explicit opt-in for unlimited; add jitter, stop reconnecting after repeated handshake failures, and surface a "server keeps crashing — stop?" prompt.

### MB-075 — Medium, Confirmed — MCP: no inbound size limits before parsing (stdio / HTTP / SSE / registry); the ToolCalling 1 MiB cap is post-parse only

- **Sources:** Part 3 V-34
- **Files:** `app/mcp/MCPClient.cpp:288-292` (`handleStdioStdout`: unbounded `while(canReadLine())` + `QJsonDocument::fromJson(line)`), `:355` (`onHttpPostFinished`: `reply->readAll()`), `:417-419` (`onSseReadyRead`: `m_sseBuffer += readAll()`, no cap, only split on `"\n\n"`), `:407,457` (JSON parse of the full body/SSE block); `app/mcp/MCPRegistry.cpp:81,96,157,179` (`reply->readAll()`); `app/mcp/ToolCalling.cpp:201-210`
- **CWE:** CWE-400, CWE-770
- **Evidence:** No `max*Bytes`, `Content-Length`, or line-length check in any of the 8 MCP files. The ToolCalling cap is real but too late: `bytes=QJsonDocument(result).toJson(Compact); if bytes.size()>m_maxOutputBytes {r.truncated=true; replaces output}`.
- **Impact:** A malicious MCP server/registry can OOM or CPU-starve the app with a multi-hundred-MB single line, HTTP body, or never-terminated SSE stream (the SSE buffer grows until `"\n\n"` arrives).
- **Remediation:** Cap stdio lines (for example 1 MiB), drop/kill the server on excess, cap `m_sseBuffer` and HTTP bodies (abort the reply over, for example, 10 MiB), cap registry responses, and enforce the cap **before** `fromJson()` — keep the ToolCalling post-parse cap as a second layer.

### MB-076 — Medium, Confirmed — MCP: tool-name/schema collisions — no validation, no dedup, first-match wins

- **Sources:** Part 3 V-35
- **Files:** `app/mcp/MCPClient.cpp:577-587` (`tools/list`: `tool.name = t.value("name").toString()` — no pattern/uniqueness check, appended blindly); `app/mcp/ToolCalling.cpp:143-152` (first `t.name==toolName` wins), `:174` (`m_pendingByTool[toolName]`)
- **CWE:** CWE-20, CWE-843
- **Evidence:** Empty, duplicate, or colliding names (for example a server advertising `read_file`, `run_terminal`) are accepted; `ToolCalling::call()` silently uses the first match and never validates that the tool exists (an unknown tool → empty schema → validation skipped, see MB-077).
- **Impact:** A malicious server can shadow a legitimate tool of the same name, or (once MCP tools are merged with built-in tools — not yet the case) hijack calls intended for a trusted tool.
- **Remediation:** Validate tool names against `^[A-Za-z0-9_.-]+$`, reject/rename duplicates with a per-server namespace (for example `serverId::toolName`), and fail `call()` loudly for unknown tools instead of proceeding with an empty schema.

### MB-077 — Medium, Confirmed — Argument validation is type/required/enum only; unknown arguments, sizes, patterns, and array contents are unchecked; ToolCalling validation fails open

- **Sources:** Part 3 V-36
- **Files:** `app/tools/ToolRegistry.cpp:104-180,201-206`; `app/mcp/ToolCalling.cpp:49-61` (`checkType`), `:63-135` (`validateArgs`)
- **CWE:** CWE-20, CWE-703
- **Evidence:** ToolRegistry: `// Unknown constraints do not fail validation. return true;` (`:114-115`); `validateArgs()` checks type, required, enum, and nested object properties only. No `additionalProperties`, `min/maxLength`, `pattern`, `minimum/maximum`, `items`, or size checks. `if (schema.isEmpty()) return result;` (`:118-122`) — an empty schema accepts anything. ToolCalling: `// Unknown/unsupported type constraint: do not fail validation. return true;` (`:59-61`); `:68` — an empty schema passes; only top-level type, required, per-property type/enum, and recursion into nested objects. No `additionalProperties`, array items, `pattern`/`format`, min/max, `oneOf`/`anyOf`, type-as-array, or depth/size limits. Recursion (`:122-129`) has no depth cap.
- **Impact:** A model can pass extra arguments the schema never declared (the tool implementation may still consume them), oversized strings, and out-of-range numbers. Validation therefore cannot be relied on as a security boundary for path/command arguments. A hostile schema + arguments can recurse deeply.
- **Remediation:** Reject unknown properties (`additionalProperties:false` by default), enforce `min/maxLength`, `pattern`, numeric bounds, array `items`/`maxItems`, require every tool to declare a non-empty schema, use a real JSON-Schema validator (or at least enforce `additionalProperties:false` when declared, `items`, `pattern`, string/array length bounds, and a max nesting depth of, for example, 32); treat an empty/missing schema for a known tool as "deny", and unknown tools as "deny".

### MB-078 — Medium, Confirmed — MCP: arbitrary custom headers can override `Authorization` / `Host` / `Cookie` and are persisted in plaintext

- **Sources:** Part 3 V-37
- **Files:** `app/mcp/MCPClient.cpp:223-225` (`openSseStream`), `:273-275` (`sendHttpPost`); `app/mcp/MCPServerManager.cpp:64,80-81`
- **CWE:** CWE-113, CWE-200
- **Evidence:** `for (it=m_httpCustomHeaders.cbegin();…) req.setRawHeader(it.key(), it.value());` runs **after** the Bearer header is set, so a custom `Authorization` header can replace it. There is no blocklist for `Host`/`Cookie`/`Authorization`/`Content-Length`. The headers are also saved in plaintext in `servers.json` (MB-027).
- **Impact:** A config containing malicious `customHeaders` (MB-007 / MB-027) can smuggle credentials or override auth on the HTTP transport.
- **Remediation:** Blocklist `Authorization`, `Host`, `Cookie`, `Content-Length`, `Mcp-Session-Id` (and any header the client manages) from `customHeaders`; apply custom headers **before** managed ones; store sensitive headers in the credential store (MB-027).

### MB-079 — Medium, Confirmed — `maxOutputBytes` is a dead control: output is never limited or truncated

- **Sources:** Part 3 V-38
- **Files:** `app/tools/ToolRegistry.h:94,140`; `app/tools/ToolRegistry.cpp:283-288,362-364`
- **CWE:** CWE-400
- **Evidence:** `int m_maxOutputBytes=1024*1024;` (`.h:140`) — a `setMaxOutputBytes` setter exists. `onToolOutput()` forwards every chunk unbounded: `emit toolOutput(callId, it->name, stream, chunk);` — `m_maxOutputBytes` is never read. `grep` shows it referenced only in the getter/setter/member declaration.
- **Impact:** A tool (or a prompt-injected model driving a read/search tool) can stream unbounded output into the UI/agent context, causing memory exhaustion and context flooding. `ToolResult.truncated` is never set by the registry.
- **Remediation:** Enforce the cap in `onToolOutput` (accumulate a per-call byte count, drop/truncate beyond the cap, set `truncated=true`, optionally cancel the tool) and cap `ToolResult.output` size in `finishCall`.

### MB-080 — Medium, Confirmed (in-scope absence); end-to-end loop limit not verified — No limits on concurrency, pending approvals, or tool-loop volume; the watchdog does not cover the approval wait

- **Sources:** Part 3 V-39
- **Files:** `app/tools/ToolRegistry.cpp:182-188,228-248,317-328`; `app/tools/ToolApproval.cpp:92-98`
- **CWE:** CWE-400
- **Evidence:** `execute()` inserts into `m_running` before validation/unknown-tool checks (`:183-188`); there is no `maxConcurrentCalls`, no per-call/total-call budget, and no cap on `m_pending`/`m_running` growth. The watchdog timer is created only in `beginExecution()` (`:233-243`) — a call awaiting approval can sit in `m_pending`/`m_running` forever. `setDefaultTimeoutMs(ms<=0)` disables the watchdog entirely (`:358-360`).
- **Impact:** Unbounded tool-call loops/fan-out (the classic agent-loop resource-exhaustion pattern) are not stopped by anything in these files; memory grows with pending calls. If the orchestrator has no step/call budget either, this is directly exploitable by prompt injection.
- **Remediation:** Add a configurable max-concurrent-calls and max-calls-per-run budget, a timeout for pending approvals (auto-deny + log on expiry), and reject `setDefaultTimeoutMs(<=0)` from non-test code.

### MB-081 — Medium, Confirmed in code; tool compliance not verified — Watchdog / `cancel()` is best-effort only; a tool that ignores cancel keeps running after the registry has forgotten it

- **Sources:** Part 3 V-40
- **Files:** `app/tools/ToolRegistry.h:47`; `app/tools/ToolRegistry.cpp:317-328,334-342`
- **CWE:** CWE-400, CWE-362
- **Evidence:** `virtual void cancel(qint64) {}` — best effort. `onWatchdog()`: `if it->tool it->tool->cancel(callId);` — which may be a no-op; then `ToolResult r; r.error="Tool execution timed out"; finishCall(callId,r);` erases `m_running`, and a later `finished()` is dropped.
- **Impact:** On timeout the registry reports "cancelled" and drops the call, but a non-cooperative tool (a spawned process in TerminalTool/CodeRunner) may continue executing/writing files in the background, invisible to the agent/UI. The user-visible state ("timed out") is then false.
- **Remediation:** Make `cancel()` mandatory/verified for process-backed tools (kill the process group, verify termination — MB-039), keep a "zombie" record until the tool actually emits `finished()`, and surface orphaned completions in the log rather than silently dropping them.

### MB-082 — Medium, Confirmed exposure; bypass-by-model Suspected — Direct QML exposure of terminal/code-runner objects bypasses the registry and approval gate entirely

- **Sources:** Part 3 V-41; Part 1 F-21 (merged — QML auto-start / no-approval run, the same exposure)
- **Files:** `app/Application.cpp:84-89`; `app/ui/TerminalView.qml:202,252-254,341`; `app/ui/CodeRunnerView.qml:47,155`; `app/ui/TerminalView.qml:208-213` (`Component.onCompleted: root.session.start(...)`), `app/ui/CodeRunnerView.qml:43-52` (`runner.run(...)` directly)
- **CWE:** CWE-862
- **Evidence:** `ctx->setContextProperty("agent", m_agent); ("terminalSession", m_terminalSession); ("codeRunner", m_codeRunner);` QML calls them directly, with no approval: `root.session.start(root.startDirectory,80,24,{})`, `root.session.sendText(line+"\n")`, `root.runner.run(root.language, root.code, "")`. The terminal auto-starts a shell on load (`TerminalView.qml:208-213`), and CodeRunner runs code with no approval in QML (`CodeRunnerView.qml:43-52`). The C++ claims (`QProcess` arg lists, `QTemporaryDir`, PTY) were out of scope for the QML reviewer and not verified there.
- **Impact:** Parallel ungated execution paths to the same capabilities the tool gate is meant to control. They are user-driven UI paths by design, so this is not by itself a prompt-injection bypass — but any path by which model output, a skill, or injected QML/content can reach these context properties (or `agent.approve()` / `agent.approval()` reachable from QML) inherits full terminal/code execution with no approval, no preview, and no audit log.
- **Remediation:** Route terminal/code execution initiated from agent/model context through `ToolRegistry::execute()`; do not expose raw `TerminalSession`/`CodeRunner` QObjects to QML, or expose a narrow façade whose methods re-check approval; audit all QML call sites reachable with model-controlled strings. Show the shell/workdir and require an explicit Start/Run; add C++ tests.

### MB-083 — Medium, Confirmed — Filesystem: no sensitive-file / executable-hook exclusion inside the sandbox

- **Sources:** Part 3 V-42
- **Files:** `app/tools/FileSystemTools.cpp:122-157` (read), `:201-260` (write), `:299-379` (edit), `:553-565` (search); no denylist found by grep in any scope file
- **CWE:** CWE-200, CWE-732
- **Evidence:** Even with a correct root, the agent can read `.env`, `.git/config`, private keys and credentials stored in a project, and can write `.git/hooks/*` (executed automatically on the user's next git operation), startup scripts, and config files — a write needs no further approval beyond the generic Write level.
- **Impact:** Partly by design for a coding agent, but there is no boundary between "source to edit" and "secrets / code-execution hooks".
- **Remediation:** Deny read/write/search of a configurable blocklist (`.env*`, `.git/hooks/`, `*.pem`, `id_rsa*`, `credentials*`, `*secret*`) by default, with an explicit user override; treat hook/config writes as a higher approval level.

### MB-084 — Medium, Confirmed — Filesystem: `write_file` creates directories outside the sandbox *before* the symlink re-check

- **Sources:** Part 3 V-43
- **Files:** `app/tools/FileSystemTools.cpp:229,236-245`
- **CWE:** CWE-22, CWE-59
- **Evidence:** `if args.createDirs.toBool(true) { if !QDir().mkpath(fi.absolutePath()) … }` then `// Re-check final parent after mkpath… if !sandboxRoot().isEmpty() { resolvePath(fi.absolutePath(), &parentError); if !parentError.isEmpty() refuse write }`. `mkpath()` follows intermediate symlinks, so `root/link->/outside/a/b` creates `/outside/a/b` as a side effect; the later re-check refuses only the file write, and the directories already exist.
- **Impact:** Directory creation outside the sandbox, even when the file write itself is refused.
- **Remediation:** Canonicalise and `isInsidePath`-check the parent **before** `mkpath()`; reject if any existing parent component is a symlink escaping the root; only then create missing (non-symlink) parents.

### MB-085 — Medium, Suspected — Filesystem: systemic check-then-open TOCTOU; no `O_NOFOLLOW` / re-validation after open

- **Sources:** Part 3 V-44
- **Files:** `app/tools/FileSystemTools.cpp:122/136` (read), `:201/247` (write), `:299/371` (edit); `app/tools/FileEditHistory.cpp:38-68` (undo, see MB-090)
- **CWE:** CWE-367, CWE-59
- **Evidence:** Every tool canonicalises at `resolvePath()` time, then opens the path by name later. `canonicalOrClean()` (`FileSystemTools.cpp:49-53`) is worse for non-existent paths: it returns a purely lexical `cleanPath()` when `QFileInfo::exists()` is false, so intermediate symlinks are unresolved until the file exists.
- **Impact:** A concurrent local writer swapping a file/parent for a symlink in between defeats the check. The window is small and requires a local writer, so Suspected, not demonstrated — but the same pattern is in all four tools.
- **Remediation:** Open with `O_NOFOLLOW` (or the Qt equivalent), re-canonicalise via `/proc/self/fd/<fd>` after open, and re-check containment before reading/writing; for new files, resolve the canonical parent first and append the basename.

### MB-086 — Medium, Suspected — `QSaveFile::setDirectWriteFallback(true)` follows symlinks on rename failure

- **Sources:** Part 3 V-45
- **Files:** `app/tools/FileSystemTools.cpp:248` (write), `:372` (edit); `app/tools/FileEditHistory.cpp:63` (undo)
- **CWE:** CWE-59
- **Evidence:** `QSaveFile file(path); file.setDirectWriteFallback(true);` Normally `QSaveFile` renames a temp file over the destination (replacing the symlink itself, not its target). Only if the rename fails does the fallback write directly to the path, following any symlink planted there. Suspected because the fallback path was not triggered in a read-only review.
- **Impact:** A planted symlink can redirect a write to an arbitrary file when the atomic rename fails.
- **Remediation:** Remove `setDirectWriteFallback(true)` everywhere; fail the write if the atomic rename/commit fails.

### MB-087 — Medium, Confirmed — Filesystem: unbounded writes and unbounded undo-history memory

- **Sources:** Part 3 V-46
- **Files:** `app/tools/FileSystemTools.cpp:218` (`before=previous.readAll()`), `:322` (`before=file.readAll()`), `:255-260` (content `toUtf8()` written with no size bound); `app/tools/FileEditHistory.cpp:19-29`, `app/tools/FileEditHistory.h:46` (`m_limit=100` entries, count-only)
- **CWE:** CWE-400, CWE-770
- **Evidence:** `write_file`/`edit_file` impose no limit on content size; `FileEditHistory::record()` stores the full before **and** after byte arrays for every edit, capped only at 100 entries — for example 100 × multi-MB edits is unbounded memory growth and disk fill. Contrast the good caps: `read_file` 1 MB (`:142`), `search_code` 2 MB/file (`:532`).
- **Impact:** Memory exhaustion and disk fill from large writes/edits.
- **Remediation:** Cap write/edit content (for example 1–2 MB, matching read), cap the before-snapshot stored for undo, enforce a total history byte budget in addition to the 100-entry count, and refuse/truncate large files in undo.

### MB-088 — Medium, Confirmed — Filesystem: `search_code` has an unbounded tree scan and an unbounded regex

- **Sources:** Part 3 V-47
- **Files:** `app/tools/FileSystemTools.cpp:530-533,538-546,553-600`
- **CWE:** CWE-400, CWE-1333
- **Evidence:** Output is capped (`maxResults` 1–5000, `contextLines` 0–20, 2 MB/file, binary skipped `:565`), but there is **no** cap on files searched, total bytes read, or elapsed time: a query with no matches scans the entire tree (including `.git/`, `node_modules/`, vendored trees). A user/LLM-controlled regex (`useRegex`, `:538-546`) has no timeout, so a catastrophic-backtracking pattern is applied per line per file. `edit_file`'s regex (`:336-360`) has the same no-timeout issue on a single file (lower impact).
- **Impact:** CPU/memory exhaustion from a single search, and regex denial-of-service.
- **Remediation:** Cap files searched and total bytes scanned, skip `.git/`, `node_modules/`, and other large generated dirs by default, exclude secret filenames, and set a regex match timeout / complexity limit.

### MB-089 — Medium, Confirmed (hang on special files: Suspected) — FileWatcher has no confinement and hashes whole files unbounded

- **Sources:** Part 3 V-48
- **Files:** `app/tools/FileWatcher.cpp:13-16,35-54,99-112`; `app/tools/FileWatcher.h:34`
- **CWE:** CWE-22, CWE-400, CWE-73
- **Evidence:** `return QDir::fromNativeSeparators(QDir::cleanPath(QDir().absoluteFilePath(path)));` — only lexical normalisation, no canonicalisation, no root. `hashOf()`: `QFile file(path); file.open(ReadOnly); QCryptographicHash hash(Md5); hash.addData(&file);` — reads to EOF, with no size limit. `watch()` is `Q_INVOKABLE` and exposed to QML, yet accepts any path (`~/.ssh/*`, `/etc/*`, …), also watches the parent directory of every file (`watchDirectoryOf`, `:99-105`), hashes the entire file on every change/rescan with no size cap, no watched-count cap, and no regular-file check. MD5 (`:110`) is weak for change detection, though secondary. A hang on `/dev/zero` / FIFOs is Suspected — `QFile` behaviour on special files was not executed.
- **Impact:** Any file on the system can be watched and fully hashed; a special file can hang the watcher.
- **Remediation:** Give FileWatcher the same sandbox root and canonical containment check as FileToolBase; refuse non-regular files and files over a size cap; cap the watched set; use SHA-256.

### MB-090 — Medium, Suspected — `undoLast()` trusts the stored path and never re-validates it

- **Sources:** Part 3 V-49
- **Files:** `app/tools/FileEditHistory.cpp:31-76`
- **CWE:** CWE-22, CWE-367
- **Evidence:** `const Entry entry=m_entries.takeLast(); QFile current(entry.path);` — no `resolvePath`/root check; `const QByteArray actual=current.readAll();` (`:45`); `if actual!=entry.after refuse` (`:47-53`) — a genuine safeguard, but TOCTOU; `QSaveFile file(entry.path); file.setDirectWriteFallback(true);` (`:62-63`). The "changed since edit, refuse" check is genuine, but the path is never re-resolved: if the workspace/root changed since the edit, or a file was swapped for a symlink whose target content happens to equal `entry.after`, undo writes `entry.before` outside the original root (for a never-existed file, `current.remove()` `:57` removes the symlink itself — that branch is safe).
- **Impact:** Undo can write outside the sandbox root.
- **Remediation:** Store the sandbox root at `record()` time; in `undoLast()`, re-canonicalise `entry.path`, verify containment, re-verify content after opening, and drop the direct-write fallback.

### MB-091 — Medium, Confirmed missing validation; destructive effect Suspected — Skills: `installed.json` is trusted blindly; `uninstallSkill` recursively deletes an unchecked stored path

- **Sources:** Part 3 V-50
- **Files:** `app/skills/SkillsManager.cpp:300-330` (`loadState`), `:207-215` (`uninstallSkill`), `:38-55` (`copyTree`), `:124-152` (`installSkill`); `app/core/PathSecurity.cpp:8-17` (reference)
- **CWE:** CWE-22, CWE-73, CWE-732
- **Evidence:** `loadState` (`:305-330`): `s.id=o.value("id").toString(); s.path=o.value("path").toString(); s.enabled=o.value("enabled").toBool(false);` — no `isInside()` check, no id check. `uninstallSkill` (`:207-212`): `QString path=m_skills[i].path; m_skills.removeAt(i); if !path.isEmpty() QDir(path).removeRecursively();` — an arbitrary tree delete. `installSkill` (`:147`): the previous install is deleted **before** the new copy is verified: `QDir(target).removeRecursively(); if !copyTree(sourcePath,target) { emit skillError; return; }` — a partial copy is left behind. `copyTree` (`:38-55`): no total size/file-count limit, no symlink check; `SkillInjector` reads `skill.path+"/SKILL.md"` and `scanPath(path)` likewise trust the stored path. `PathSecurity::isInside()` exists but **no** in-scope file calls it (`grep`: only `ModelDownloader.cpp` does — and it only includes the header).
- **Impact:** Anyone able to write `installed.json` (a local process, a tampered backup/profile, or a future skill-install path) can set a skill's path to, for example, the user directory or a drive root; invoking Remove then recursively deletes it. Separately, installing a huge source tree can fill the disk, and a symlinked source may pull in outside files (Suspected — symlink behaviour of `QDir::entryInfoList`/`QFile::copy` was not runtime-verified).
- **Remediation:** On `loadState`, validate every record (id same check as install, path must satisfy `PathSecurity::isInside(skillsDir,path)` after canonicalisation, `enabled` only for known ids); in `uninstallSkill`, re-validate before `removeRecursively()`; in `installSkill`, copy to a temp dir and swap in only on success; cap total size/file count and refuse/skip symlinks.

### MB-092 — Medium, Confirmed — SkillInjector delimiters are spoofable; header fields and content are injected raw (latent)

- **Sources:** Part 3 V-51
- **Files:** `app/skills/SkillInjector.cpp:23-36` (delimiters), `:44-100` (`buildBlock`), `:60-72` (read+header); `app/skills/SkillInjector.h:39-42`
- **CWE:** CWE-1426, CWE-117
- **Evidence:** `QString skillStartDelimiter(skillId) { return "<<<BEGIN UNTRUSTED SKILL: "+skillId+">>>"; }` — the raw id; `header = skillStartDelimiter(id)+QString(" name=\"%1\" version=\"%2\"").arg(skill.name,skill.version)+"\n"; content=QString::fromUtf8(file.readAll());` — `SKILL.md`, unbounded, verbatim; `skillBlock=header+content+"\n"+footer;`. A skill's `SKILL.md` (or frontmatter name/version, or id) can contain a literal `<<<END UNTRUSTED SKILL: …>>>` / fake `<<<BEGIN …>>>` block and close the quoted region early, or break the header with quotes/newlines. The 4000-token budget (`SkillInjector.h:42`) and truncation marker are good mitigations for size, not for spoofing.
- **Impact:** Delimiters are a labelling convention, not a security boundary — a malicious skill can forge the end of its own block and present following text as trusted/system content to the model. Currently no consumer of `buildContextBlock()` was found, so this is latent.
- **Remediation:** Strip/escape delimiter strings from content, skillId, name, and version before assembly (or reject skills containing them); use a per-run random nonce in delimiters; cap `SKILL.md` size before `readAll()`; treat the block as untrusted data in the system prompt, not as instructions.

### MB-093 — Medium, Confirmed — Agent/skills: multiple unbounded inputs / no timeouts (resource exhaustion)

- **Sources:** Part 3 V-52
- **Files:** `app/agent/AgentMemory.cpp:201-217` (`fromJson`), `:511-517+543-560` (`searchTool`), `:456/463` (`recall`), `~580-640` (llama embedding provider); `app/agent/AgentPlanner.cpp:89-91` (`setMaxSteps`); `app/agent/AgentOrchestrator.cpp:123-140` (`onToolOutput`); `app/skills/SkillsManager.cpp:38-55,166` (copy/readAll); `app/skills/SkillsShClient.cpp:100,120,148` (see MB-036)
- **CWE:** CWE-400, CWE-770
- **Evidence:** `q.topK = std::max(1, args.value("top_k").toInt(8));` — no upper bound; `semanticWeight` unclamped (`:517`); `searchTool` (`:543-560`) returns **all** scratchpad + **all** project-context entries, with no limit; `const int maxNotesVal=data.value("maxNotes").toInt(m_maxNotes); if maxNotesVal>0 m_maxNotes=maxNotesVal;` (`fromJson` `:212-214`, no cap); the notes array is loaded whole first; `void AgentPlanner::setMaxSteps(int count){if(count>0) m_maxSteps=count;}` (`:89-91`, no cap); the embedding provider: `n_ctx=std::max(32,nTok+8);` creates a new `llama_context` per call, with no input cap; `onToolOutput` emits `agentToken(chunk)` per chunk with no total cap/timeout for the run.
- **Impact:** A model/tool caller passing a huge `top_k`, a crafted memory JSON with huge `maxNotes`/notes, a huge scratchpad value, or a huge embedding input can cause large allocations, long synchronous `llama_context` creation, or unbounded streaming. Local-app DoS; not remote RCE on its own.
- **Remediation:** Clamp `top_k` (for example ≤50), `semanticWeight` to `[0,1]`, `maxNotes`/`setMaxSteps` to sane maxima, cap scratchpad/project values and `searchTool` result counts, cap embedding input tokens and reuse one `llama_context`, and add per-tool/per-run timeouts and a total output cap.

### MB-094 — Medium, Suspected — Git: `pathArgument` / `isInsideRoot` are lexical-only containment, with a symlink bypass

- **Sources:** Part 3 V-53
- **Files:** `app/tools/GitTools.cpp:42-52` (`isInsideRoot`), `:55-71` (`pathArgument`); used by diff (`:239-245`) and log (`:423-429`)
- **CWE:** CWE-59, CWE-22
- **Evidence:** `const QString absolute = info.isAbsolute()?input:QDir(root).absoluteFilePath(input); const QString clean=QDir::cleanPath(absolute); if !isInsideRoot(root,clean) reject; *relative=QDir(root).relativeFilePath(clean);` — `cleanPath()` is purely lexical, with no `canonicalFilePath()`, no symlink resolution, and no `lstat`. See also MB-020.
- **Impact:** A symlink inside the repo pointing outside (for example `link->/etc`, `path="link/passwd"`) passes the lexical check. Exploitability via `git diff`/`log -- <path>` was not proved — git's pathspec handling of symlinked directories is restrictive — hence Suspected, not Confirmed exploitable.
- **Remediation:** Canonicalise with `QFileInfo::canonicalFilePath()` (and canonicalise the root once in `resolveRepoRoot()`, `:106-117`), resolve each path component, re-check containment on the canonical path, and reject symlinked components.

### MB-095 — Medium, Confirmed echo; exfiltration Suspected — Git: credential/token echo in the command and in stderr/error

- **Sources:** Part 3 V-54
- **Files:** `app/tools/GitTools.cpp:73-81` (`joinCommand`), `:83-92` (`commandOutput`), `:163,165-170` (`runGit`)
- **CWE:** CWE-200, CWE-532
- **Evidence:** `o["command"]=command; o["stdout"]=…; o["stderr"]=…;` (`commandOutput`); `r.output=commandOutput(joinCommand(arguments),out,err,exitCode,truncated); r.error=QString::fromUtf8(err).trimmed();` on failure. If the remote (MB-008) is `https://user:token@github.com/…`, that URL is in `output.command`, and git's own error text can repeat it in `stderr`/`error`. There is no redaction anywhere in this file.
- **Impact:** A token/credential ends up in the `ToolResult` returned to the LLM and (depending on unreviewed logging) in logs/transcripts — the standard credential-leak path once MB-008 allows a URL remote.
- **Remediation:** Reject credentials in URLs (MB-008 allowlist); redact `://[^@]*@` / `--password=…` patterns from command/stdout/stderr/error before storing; never include the full reconstructed command in output, or include only the subcommand name.

### 8.3 Core, storage, models, inference, hardware, chat — Medium

### MB-096 — Medium, Confirmed — Updater: unbounded download, the whole file held in RAM twice, size check only after the disk write

- **Sources:** Part 4 M1
- **Files:** `app/core/Updater.cpp:240, 269, 246-254`
- **CWE:** CWE-400, CWE-770
- **Evidence:** `f.write(reply->readAll());` (`:240`, no size cap during download); `const QByteArray data = df.readAll();` (`:269`, the entire file again for `verifySignature()`); `if (m_expectedSize > 0) { QFileInfo fi(m_downloadPath); if (fi.size() != m_expectedSize) … }` (`:246-254`, after the write). No `Content-Length`/max-size abort during transfer, no streaming hash.
- **Impact:** A malicious/compromised manifest can cause disk-fill and multi-hundred-MB RAM spikes — local DoS.
- **Remediation:** Abort if `downloadProgress` total or accumulated bytes exceed `max(m_expectedSize, sane cap)`; stream SHA-256 while writing instead of `readAll()`; stream or chunk the signature verification instead of loading the whole file.

### MB-097 — Medium, Confirmed — Update manifest itself is unsigned; trust rests solely on TLS and the GitHub account

- **Sources:** Part 4 M2
- **Files:** `app/core/Updater.cpp:115-179` (especially `:140-167`)
- **CWE:** CWE-494, CWE-347
- **Evidence:** The manifest's `version`/`url`/`sha256`/`signature`/`size` fields are accepted on the strength of an HTTPS fetch from `github.com/kRxZykRxZy/ModelBench/releases/latest/download/update-manifest.json` (`:28-29`). The signature in the manifest covers the *artefact*, not the manifest — a compromised release (or a TLS failure) can point the client at an arbitrary HTTPS URL with a matching SHA-256 of the attacker's choosing. The HTTPS checks on the manifest URL, final URL, and download URL (`:95, 123-129, 156, 187, 227`) are good, but they are the only gate.
- **Impact:** Account compromise / release tampering = an attacker-chosen update URL; the artefact signature is then the only remaining defence, and it is currently broken (MB-044).
- **Remediation:** Sign the manifest itself (a detached Ed25519 signature over the canonical manifest bytes, verified with the real key before parsing) or fetch it via the GitHub API with the release's own integrity data; pin the expected host (`github.com` / `objects.githubusercontent.com`) and reject manifest URLs pointing elsewhere.

### MB-098 — Medium, Confirmed client-side; backend IDOR Suspected — `WhopClient`: no HTTPS enforcement, raw `userId` in the query string, no auth, unchecked checkout URL

- **Sources:** Part 4 M3
- **Files:** `app/billing/WhopClient.cpp:14, 22, 49, 33-36, 43-56`; `app/billing/WhopClient.h:14`
- **CWE:** CWE-319, CWE-74, CWE-862, CWE-601
- **Evidence:** `void WhopClient::setBackendBaseUrl(const QString &url) { m_backendBaseUrl = url; }` — no validation. `QUrl url(m_backendBaseUrl + "/billing/checkout");` (`:22`). `QUrl url(m_backendBaseUrl + "/billing/entitlements?userId=" + userId);` (`:49`, no encoding). `QString u = doc.object().value("checkoutUrl").toString(); else emit checkoutSessionReady(u);` (`:34-36`, no scheme/host check). `setBackendBaseUrl()` accepts `http://`; `fetchEntitlements()` interpolates `userId` unencoded (`&`, `?`, `#`, spaces inject extra query parameters); neither call sends any `Authorization` header — entitlements are keyed only by a client-supplied `userId` (whether another user's entitlements can be fetched depends on the Node backend — Suspected, needs a server check; see MB-011, which confirms the server side); `checkoutUrl` from the server is emitted verbatim with no `https://` + allowlist check before a UI presumably opens it (no QML consumer was found in-repo).
- **Impact:** Query-parameter injection, MITM/phishing via a compromised/HTTP backend, possible IDOR.
- **Remediation:** Enforce `https://` for the backend base URL; use `QUrlQuery::addQueryItem("userId", userId)`; send the `AccountManager` Bearer token on billing calls and enforce it server-side; validate `checkoutUrl` is `https://` on an allowlisted host before emitting/opening. Positive note: no Whop secret is in the client (`WhopClient.h:14` comment is honoured).

### MB-099 — Medium, Confirmed missing controls; Suspected token leakage via redirect (not proven) — Hugging Face client: no redirect policy, timeout, or response-size limit; the token is session-only but the UI claims otherwise

- **Sources:** Part 4 M4
- **Files:** `app/models/HuggingFaceClient.cpp:14-22, 24-57, 59-82`; `app/models/HuggingFaceClient.h:15, 28`
- **CWE:** CWE-400, CWE-319
- **Evidence:** `makeRequest()` (`:16-22`) attaches `Authorization: Bearer <token>` whenever `m_token` is set, but no `RedirectPolicyAttribute`, timeout, or size cap is set on any request in this file. `setToken()` (`:14`) stores the token in a plain `QString` member; it is never persisted — which is good — yet the Settings UI text says it is "Stored securely in OS credential storage", which is false (the token is lost on restart and is *not* in `SecretStorage`).
- **Impact:** A large/malicious API response can exhaust memory; a redirect to another host risks carrying the `Authorization` header with it (Qt's exact header-stripping behaviour on cross-host redirects was not verified here — treat as suspected, not confirmed). The UI claim misleads users about where the token lives.
- **Remediation:** Set `NoLessSafeRedirectPolicy`, a timeout (for example 30 s), and a response-size cap; clear `m_token` on logout; either actually store the token via `SecretStorage` (DPAPI on Windows / Keychain / libsecret elsewhere) or correct the UI text to "session only, not saved".

### MB-100 — Medium, Confirmed — HF `repoId` is concatenated raw into the API URL (path/query injection)

- **Sources:** Part 4 M5
- **Files:** `app/models/HuggingFaceClient.cpp:59-61`; `app/ui/ModelsView.qml:289`
- **CWE:** CWE-74, CWE-20, CWE-918
- **Evidence:** `void HuggingFaceClient::fetchModelFiles(const QString &repoId) { // Q_INVOKABLE, .h:17 QString url = "https://huggingface.co/api/models/" + repoId + "/tree/main"; }` No percent-encoding or validation. Contrast `searchModels()` (`:24-32`), which correctly uses `QUrlQuery`. A `repoId` containing `../`, `?`, `#`, or encoded separators can escape `/api/models/<repo>/tree/main`, add query parameters, or reach other `huggingface.co` API paths — with the user's `Bearer` token attached (`makeRequest()`:20). The same pattern exists in the QML download URL (`ModelsView.qml:289`: `"https://huggingface.co/" + selectedModelId + "/resolve/main/" + modelData.name`). Combined with MB-010, a crafted file name (notably `\` on Windows, which the QML `split('/').pop()` does not strip) can also traverse the download destination.
- **Impact:** Limited to the `huggingface.co` origin (the host is fixed), but an attacker-influenced repo id/file name can cause the client to request unintended API endpoints or files with the user's token.
- **Remediation:** Validate `repoId` against `^[\w.-]+/[\w.-]+$` (reject `/`, `..`, `?`, `#`, `%`); build the URL with `QUrl` + `QUrl::toPercentEncoding` per path segment; do the same for the QML download URL construction; fix MB-010 so a traversal in the file name cannot escape the models directory regardless.

### MB-101 — Medium, Confirmed — `PathSecurity::isInside()` does not resolve symlinks (and is effectively dead code)

- **Sources:** Part 4 M6; Part 3 V-75 (merged — the same helper, rated Medium there)
- **Files:** `app/core/PathSecurity.cpp:8-15, 17-23`
- **CWE:** CWE-59, CWE-61, CWE-73
- **Evidence:** `bool isInside(const QString &root, const QString &child) { QString r = QDir(root).absolutePath(); QString c = QFileInfo(child).absoluteFilePath(); // lexical only, no canonicalisation if (!c.startsWith(r)) return false; if (c.length() > r.length() && c[r.length()] != '/') return false; return true; }` `absoluteFilePath()` does not resolve symlinks, so `root/link -> /etc` passes. The boundary check uses `'/'` only (wrong on Windows `\`, and false-negatives when `root` has a trailing slash or is `/`), and `startsWith` is case-sensitive on case-insensitive filesystems. Its sole in-repo consumer, `app/models/ModelDownloader.cpp:2`, merely `#include`s the header and never calls `isInside()`/`sanitizeFileName()`. `sanitizeFileName()` (`:17-23`) is a denylist, not an allowlist: `out.replace('/', '_'); out.replace('\\', '_'); out.replace("..", "_");` — it does not strip NUL/control characters, `:`, leading/trailing dots/spaces, or Windows reserved names (`CON`, `NUL`, `COM1`). It does neutralise `/`, `\`, and `..` for traversal, so it is not currently exploitable for traversal on its own — but it is insufficient as a general filename sanitiser.
- **Impact:** Any caller relying on this helper for traversal/symlink containment is bypassable. Aggravating: the one place that needs it (MB-010) does not call it. In the audited files the helper is latent only, because nothing calls it (see MB-020, MB-024, MB-091) — which is why this is Medium, not High. If any (out-of-scope) tool relies on `isInside()` as a sandbox, a symlink inside the root pointing outside, or a case-variant path on Windows/macOS, escapes it.
- **Remediation:** Use `QFileInfo::canonicalFilePath()` for both root and child (after ensuring existence, or canonicalise the parent + append the leaf), compare with `QDir::separator()`, do a case-insensitive comparison on Windows/macOS, and add tests including a symlink-out case. Wire it into `ModelDownloader` (MB-010). Harden `sanitizeFileName()` to an allowlist `[A-Za-z0-9._-]` (reject/empty-check the result), strip leading dots, and reject reserved Windows names — or delete the helper so nobody adopts it, and unify on one validated helper.

### MB-102 — Medium, Confirmed missing check; Suspected exploitability — `CodeIndexer`: no symlink / root-containment check; `chunkFile()` reads any path

- **Sources:** Part 4 M7
- **Files:** `app/storage/CodeIndexer.cpp:153-176, 216, 296-330`
- **CWE:** CWE-22, CWE-59
- **Evidence:** `chunkFile(rootPath, absolutePath, …)` (`:157-161`) is public static, with no check that `absolutePath` is under `rootPath`: `QFile f(absolutePath); if (!f.open(QIODevice::ReadOnly)) return chunks;` `:175-176`: `rel` is computed but never validated for `"../"`. The `QDirIterator` at `:216` and `:296` uses `Subdirectories` with no `FollowSymlinks` flag and no `isSymLink()`/`canonicalFilePath()` check anywhere in scope.
- **Impact:** A malicious cloned repo containing a symlink to a sensitive file with an indexed extension (for example `config.json -> ~/.config/.../credentials.json`) would have the target's content indexed/stored/searched (compounds MB-053). A direct caller of public `chunkFile()` can read any file regardless of `rootPath`. Symlinked *directories* are not followed by default in Qt, so directory-escape via the iterator is less likely — that part needs a runtime check, hence Suspected for the iterator path.
- **Remediation:** In `chunkFile()`/`indexProject()`/`fileTree()`, resolve `QFileInfo::canonicalFilePath()` and reject if it does not start with `canonicalRoot + '/'`; skip symlinked files/dirs (or resolve-and-contain them), and reject `rel` starting with `..`.

### MB-103 — Medium, Confirmed — `VectorStore`: FTS5 `MATCH` query injection (search-semantics / DoS, not classic SQLi)

- **Sources:** Part 4 M8
- **Files:** `app/storage/VectorStore.cpp:321-336, 362`
- **CWE:** CWE-89 (variant), CWE-400
- **Evidence:** `"WHERE vector_entries_fts MATCH ?"` with `q.addBindValue(trimmed);` (`:328,333`) — the user query is bound, but still parsed as FTS5 query language. `:362`: "If MATCH syntax fails (e.g. special chars), fall through to LIKE." Binding prevents SQL injection, but FTS5 still interprets `"`, `*`, `OR/AND/NOT`, `NEAR`, `^`, `( )` in the bound value.
- **Impact:** A crafted query can alter which documents match, return unintended results, or trigger expensive/syntax-failing queries (for example a bare `*`, unbalanced quotes) — the code anticipates this only as a fallback. No data exfiltration beyond the already-authorised namespace (the namespace is a separate bound parameter, `:329-335`).
- **Remediation:** Sanitise/escape the FTS5 query: tokenise the input, strip FTS operators, and wrap each term in double quotes with `"` doubled, or build the query from allowlisted tokens; enforce a max query length.

### MB-104 — Medium, Confirmed — `VectorStore`: `semanticSearch()` loads the whole table; `hybridSearch()` integer overflow on `topK*3`

- **Sources:** Part 4 M9
- **Files:** `app/storage/VectorStore.cpp:249-296, 397-412, 199-217`
- **CWE:** CWE-400, CWE-190
- **Evidence:** `semanticSearch()` (`:261-269`) has no `LIMIT` — it loads every embedding, sorts in memory, then resizes: `"SELECT id, namespace, content, metadata, embedding FROM vector_entries WHERE dim > 0"`. `hybridSearch()` (`:409,412`): `semanticSearch(..., std::max(topK * 3, topK), -1.0f); keywordSearch(..., std::max(topK * 3, topK));` `listByNamespace()` (`:199-217`): `LIMIT ?` with an unclamped limit — `LIMIT -1` in SQLite means unlimited.
- **Impact:** Memory/CPU DoS on a large index (every embedding is deserialised and cosine-scored regardless of `topK`). If `topK > INT_MAX/3`, `topK*3` is signed-int overflow (UB). A negative `limit` to `listByNamespace()` loads everything.
- **Remediation:** Clamp `topK`/`limit` (for example `1..100`, with consistent `topK<=0` handling), compute `topK*3` in `qint64`, and push `LIMIT` into the SQL (with a dim-aware / ANN approach for scale).

### MB-105 — Medium, Confirmed absence in scope; Suspected whether secrets are actually stored there — Database + Repositories: secrets/settings stored in plaintext, no file permissions

- **Sources:** Part 4 M10
- **Files:** `app/storage/Database.cpp:16-26`, `app/storage/Repositories.cpp:10-17`
- **CWE:** CWE-311, CWE-732
- **Evidence:** `Database::openAndMigrate()` sets only `PRAGMA journal_mode=WAL; foreign_keys=ON;` (`:24-25`) — no encryption (`SQLCipher`/`PRAGMA key`), no `QFile::setPermissions(0600)`, no directory restriction; `grep` for `setPermissions|0600|SQLCipher|PRAGMA key` in scope returned none. `Repositories::setSetting()` stores an arbitrary `key`/`value` as plaintext `TEXT` in `app_settings` (`:13-16`).
- **Impact:** Anyone/process able to read the user's data dir (another local user, a backup, malware, a stolen laptop) reads conversations, messages, installed-model paths, and any setting value — including API keys/tokens if any caller stores them via `setSetting()` (callers are out of scope, so that is Suspected, not confirmed). WAL `-wal`/`-shm` sidecars inherit the same lax permissions.
- **Remediation:** Store secrets only via `ISecretStorage`/OS keychain, never in `app_settings`; if the DB must hold sensitive data, use SQLCipher with a key from the keychain, and `setPermissions()` `0600` on the DB file and `0700` on its directory at creation.

### MB-106 — Medium, Confirmed — `SecretStorage`: DPAPI without entropy, secrets never zeroed, blob write unchecked, non-atomic

- **Sources:** Part 4 M11
- **Files:** `app/storage/SecretStorage.cpp:21-34, 40-52`
- **CWE:** CWE-312, CWE-316, CWE-367
- **Evidence:** `:23` `CryptProtectData(&in, (service+"/"+account).toStdWString().c_str(), nullptr, nullptr, nullptr, 0, &out)` — entropy is `nullptr`, flags are `0` (user scope is the default, which is correct, but there is no optional entropy). `:32` `f.write(blob);` — the return value is ignored; no temp-file + rename, no fsync. `:45-51` `load()`: the blob is read, `CryptUnprotectData` is called, the result is returned — the plaintext `QByteArray`/`DATA_BLOB` is never `SecureZeroMemory`'d.
- **Impact:** The DPAPI blob is only as strong as the user profile (expected for DPAPI, but no extra entropy means any code running as the user can decrypt it); plaintext secrets linger in the heap after use; a crash mid-write leaves a truncated `.bin` that `load()` then fails to decrypt, with no recovery.
- **Remediation:** Pass optional entropy (or document why not), `SecureZeroMemory()` the plaintext `DATA_BLOB`/`QByteArray` after copying, check that `f.write()` wrote all bytes, and write to a temp file + atomic rename.

### MB-107 — Medium, Confirmed — `SecretStorage`: always returns the Windows implementation; silently fails on Linux/macOS

- **Sources:** Part 4 M12
- **Files:** `app/storage/SecretStorage.cpp:35-38, 53-56, 67-69`
- **CWE:** CWE-636, CWE-703
- **Evidence:** `:35-37` (non-Windows) `store()`: `Q_UNUSED(...); return false;` `:53-56` (non-Windows) `load()`: `return {};` `:67-69` `createSecretStorage() { return new WindowsSecretStorage(); }` — on every platform.
- **Impact:** On non-Windows the app silently stores nothing and loads nothing. If any caller treats `store()==false`/`load()=={}` as "no secret set" and falls back to plaintext (for example in `app_settings`, MB-105), secrets leak — that fallback was **not** found/confirmed in scope (no caller of `createSecretStorage` was found in a quick `app/` grep; callers are out of scope for this audit).
- **Remediation:** Provide real macOS (Keychain) / Linux (Secret Service/libsecret) implementations, or fail loudly at startup on unsupported platforms; never silently fall back to plaintext storage.

### MB-108 — Medium, Confirmed (regex logic) — Log redaction misses `Bearer <token>` and JSON secrets

- **Sources:** Part 4 M13
- **Files:** `app/core/Logging.cpp:10-19, 34-40`
- **CWE:** CWE-532, CWE-117
- **Evidence:** `QRegularExpression re("(?i)(token|api[_-]?key|secret|password|bearer|authorization)\\s*[:=]\\s*[^\\s,;]+"); out.replace(re, "\\1: [REDACTED]"); out.replace(QRegularExpression("hf_[A-Za-z0-9]{10,}"), "hf_[REDACTED]"); out.replace(QRegularExpression("sk-[A-Za-z0-9]{10,}"), "sk-[REDACTED]");` `Authorization: Bearer <token>` matches `authorization` + `Bearer` as the "value", so the replacement yields `authorization: [REDACTED] <token>` — the token itself remains in the log. JSON forms (`"access_token": "abc"`, `"password": "x"`) also miss, because of the `"` between the key and `:`. Bare tokens/JWTs without a label are not redacted.
- **Impact:** Access tokens/passwords are persisted to `modelfit.log` and stderr.
- **Remediation:** Redact with patterns for `Bearer\s+\S+`, `Basic\s+\S+`, JSON `"key"\s*:\s*"[^"]+"` for secret keys, and JWT-shaped strings; unit-test the redactor against those exact inputs. See also MB-164.

### MB-109 — Medium, Confirmed — `AccountManager`: false success, silent entitlement failure, logout does not revoke

- **Sources:** Part 4 M14
- **Files:** `app/core/AccountManager.cpp:35-61, 63-70, 72-86`; `app/core/AccountManager.h:19`
- **CWE:** CWE-613, CWE-754
- **Evidence:** `if (o.contains("session") && o["session"].isObject()) { m_accessToken = …; … } emit authSuccess("Account created");` (`:45`) — emitted even with no session (200 + error JSON). `emit authSuccess("Logged in");` (`:59`) — the same. `bool loggedIn() const { return !m_accessToken.isEmpty(); }` — no expiry check. `void logout() { m_accessToken.clear(); … }` — no server-side revocation call. `refreshEntitlements()`: ignores HTTP status/401 and silently keeps the old `m_tier` on failure.
- **Impact:** The UI reports "Logged in" when not authenticated; expired tokens look logged-in; a logged-out token remains valid server-side until expiry; a stale tier may gate features incorrectly.
- **Remediation:** Emit `authSuccess` only when a non-empty `access_token` was stored (otherwise `authError`); parse `expires_at` and treat expired tokens as logged-out; call a server logout/revoke endpoint; on 401 in `refreshEntitlements()`, clear the token and emit a logged-out signal.

### MB-110 — Medium, Confirmed no validation; Suspected impact (latent — not yet exploitable in audited code) — `ConversationManager::addMessage()` accepts any `role`, including `system` / `tool` (role spoofing → future prompt injection)

- **Sources:** Part 4 M15
- **Files:** `app/chat/ConversationManager.cpp:22-32`; `app/chat/ConversationManager.h:13, 23`
- **CWE:** CWE-20; future CWE-1427
- **Evidence:** The header comment lists the intended roles: `user`/`assistant`/`system`/`tool`. `addMessage` is `Q_INVOKABLE`, with no allowlist, and stores the role verbatim: `void ConversationManager::addMessage(const QString &convId, const QString &role, const QString &content) { ChatMessage msg; msg.role = role; msg.content = content; /* stored, emitted, returned by getMessages() */ }` Current QML only uses `"user"`/`"assistant"` (`ChatView.qml:40,87`).
- **Impact:** Today messages are only stored in memory. But `ChatOrchestrator.cpp:9` states the boundary is to be *"wired to InferenceManager + MCP tool loop in Phase 2/5"* — once stored messages feed an LLM, a stored `system`/`tool` message injected via this API becomes prompt injection / instruction hijack, bypassing the app's system prompt.
- **Remediation:** Allowlist `role` to `{"user", "assistant"}` here (reject anything else); only trusted app code — never QML/JS or stored data — may create `system`/`tool` messages, via a separate non-`Q_INVOKABLE` internal API; re-validate roles at the inference boundary when the LLM wiring lands.

### MB-111 — Medium, Confirmed code pattern; Suspected as independently exploitable (requires a local attacker able to swap/relink the file in the window, or a pre-planted symlink — see MB-041 for the easier path) — TOCTOU / symlink following between the existence check and the library load

- **Sources:** Part 4 M16
- **Files:** `app/hardware/BackendDetector.cpp:116-123`
- **CWE:** CWE-367, CWE-59
- **Evidence:** `const QString fullPath = QDir(dir).filePath(candidate); if (!QFileInfo::exists(fullPath)) { continue; } // line 119 QLibrary library(fullPath); if (library.load()) { ... } // line 123` `QFileInfo::exists()` follows symlinks and the result is not re-validated at load time; there is no `isSymlink()`/`canonicalFilePath()` check.
- **Impact:** The file checked is not guaranteed to be the file loaded; a symlink in a searched directory can redirect the load to an attacker-controlled library elsewhere on disk, even if the directory itself looks clean.
- **Remediation:** Drop the redundant `exists()` check; resolve once with `QFileInfo::canonicalFilePath()`, reject symlinks / paths escaping the trusted install dir, open/verify the resolved file, and load only that canonical path (fold into the MB-041 fix).

### MB-112 — Medium (latent), Confirmed missing validation; Suspected as exploitable (no current caller passes user input) — `probeLibrary()` is public with no `baseName` validation (path traversal / arbitrary library load if ever called with non-constant input)

- **Sources:** Part 4 M17
- **Files:** `app/hardware/BackendDetector.h:33-34`; `app/hardware/BackendDetector.cpp:107-118`
- **CWE:** CWE-22, CWE-73, CWE-829
- **Evidence:** `static bool probeLibrary(const QString &baseName, QString *pathOut = nullptr);` (`BackendDetector.h:34`) `probeLibrary()` (`:107-118`) — the only check is emptiness: `if (baseName.isEmpty()) { return false; } const QStringList candidates = libraryCandidates(baseName); // appends ".so"/".dll"/"lib…" const QString fullPath = QDir(dir).filePath(candidate);` `grep` across the repo shows the only in-scope caller passes hardcoded literals (`detect()`, line 239: `probeLibrary(QString::fromLatin1(probe.library), &path)` with `"ggml-cuda"` etc.), and `probeLibrary` is not `Q_INVOKABLE`/QML-exposed. But the API itself imposes no constraint: `baseName = "../../evil"` would be joined into a searched directory and loaded.
- **Impact:** Latent. Any future caller (UI, config, model/backend selection) passing user-, file-, or network-derived input would turn this into arbitrary-code-execution via library load.
- **Remediation:** Make `probeLibrary()` private, or validate strictly: reject any `baseName` containing `/`, `\`, `.`, `..`, or a path separator; allowlist exactly `{"ggml-cuda", "ggml-vulkan", "ggml-metal"}`; construct the path from the allowlisted name only.

### MB-113 — Medium, Confirmed bare program names; Suspected as exploitable (requires attacker write access to a directory earlier in `PATH`, or control of the `PATH` env var under which the app is launched) — Untrusted search path for external binaries in `HardwareDetector::runProcess()` (PATH hijacking)

- **Sources:** Part 4 M18
- **Files:** `app/hardware/HardwareDetector.cpp:30-40, 131, 227, 290, 309-319`
- **CWE:** CWE-426
- **Evidence:** `runProcess()` (lines 30-40): `QProcess process; process.start(program, arguments); // program resolved via PATH` Call sites with bare names: `runProcess("nvidia-smi", …);` (line 131), `runProcess("wmic", …);` (line 227), `runProcess("lspci", {});` (line 290), `runProcess("sysctl", …);` (lines 309, 314-316), `runProcess("system_profiler", …);` (lines 318-319). Notably, the two absolute `nvidia-smi` fallbacks (lines 133, 137: `C:/Windows/System32/nvidia-smi.exe`, `C:/Program Files/NVIDIA Corporation/NVSMI/nvidia-smi.exe`) are dead code in practice if a malicious `nvidia-smi` is found first via `PATH`.
- **Impact:** A planted binary with one of these names earlier in `PATH` executes as the app user when hardware detection runs, and its stdout is then parsed as trusted GPU/CPU data.
- **Remediation:** Use absolute paths for every helper (as already done for the Windows `nvidia-smi` fallbacks), or resolve each binary once at startup against a known system directory and refuse to run it if it is not there. Do not rely on `PATH`; consider clearing/sanitising `PATH` for child processes.

### MB-114 — Medium, Confirmed code pattern; sandbox escape Suspected (depends on the llama.cpp/minja version, out of scope) — Untrusted chat template executed by `llama_chat_apply_template`; unbounded template output

- **Sources:** Part 4 M19
- **Files:** `app/inference/ChatTemplate.cpp:11-19, 27-40, 63-76`
- **CWE:** CWE-94 (template injection), CWE-400, CWE-190
- **Evidence:** `const char *tmpl = llama_model_chat_template(model, ...); // Jinja template embedded in the GGUF const int32_t needed = llama_chat_apply_template(tmplUtf8.constData(), ...); if (needed > 0) { QByteArray out; out.resize(needed + 1); // needed is attacker-influenced, no cap const int32_t written = llama_chat_apply_template(..., out.data(), (int32_t)out.size()); }` `tmplOverride` (from the caller, no validation) takes precedence over the model template. `needed == INT32_MAX` makes `needed + 1` overflow; `written` is not checked against `out.size()`.
- **Impact:** A malicious GGUF's embedded Jinja template is executed during prompt formatting (template-injection / logic abuse; DoS via huge/looping output is the confirmed risk — unbounded allocation from `needed`). A Jinja sandbox escape would be worse but was not verified.
- **Remediation:** Cap `needed` (for example reject > 1 MiB / > some multiple of input size), check `written > 0 && written <= out.size()`, guard `needed == INT32_MAX`, and treat model-supplied templates as untrusted (pin/allowlist templates where possible; keep llama.cpp current).

### MB-115 — Medium, Confirmed — `EngineInstaller` trusts GitHub API JSON to pick an arbitrary download URL; no redirect policy

- **Sources:** Part 4 M20
- **Files:** `app/inference/EngineInstaller.cpp:56-152`
- **CWE:** CWE-20, CWE-829, CWE-918
- **Evidence:** `selectAsset()` (`:114-152`) matches asset names by `contains(osToken) && contains(archToken) && contains("cpu")` with a fallback accepting *any* `os+arch` archive, and returns `browser_download_url` verbatim; `install()` (`:154-160`) fetches it with **no `RedirectPolicyAttribute` set** (contrast `ModelDownloader.cpp:36`, which sets `NoLessSafeRedirectPolicy`). The nightly tag is extracted by regex `releases/tag/(b\d+)` (`:73-76`) — that part is safely constrained.
- **Impact:** A compromised API response (or MITM, MB-009) can point the installer at any host/archive; an open redirect could downgrade or redirect the fetch. Combined with MB-009/MB-042 this is the supply-chain entry point.
- **Remediation:** Allowlist download hosts (`github.com`, `objects.githubusercontent.com`, `release-assets.githubusercontent.com`); set `NoLessSafeRedirectPolicy` (or manual redirect handling with host validation); require the asset name to match a strict expected pattern; verify hash/signature (MB-009).

### MB-116 — Medium, Suspected (llama.cpp likely clamps some; not verified at runtime) — Unvalidated `InferenceConfig` values reach `llama.cpp`; prompt-tokenisation integer truncation and unchecked batch capacity

- **Sources:** Part 4 M21
- **Files:** `app/inference/LlamaBackend.cpp:49-76, 103-150`; `app/inference/LlamaBackend.h:18-37`
- **CWE:** CWE-20, CWE-190, CWE-1284
- **Evidence:** `mparams.n_gpu_layers = cfg.gpuLayers; cparams.n_ctx = cfg.contextLength; // no range checks, :50-60 cparams.n_threads = cfg.threads; // etc. llama_sampler_chain_add(smpl, llama_sampler_init_top_k(cfg.topK)); // topK/topP/minP/temp/seed unchecked` `generate()` (`~118-125`): `int n_prompt = -llama_tokenize(vocab, prompt.toUtf8().constData(), (int)prompt.toUtf8().size(), nullptr, 0, cfg.addBos, true); if (n_prompt < 0) n_prompt = 0; // tokenise errors silently become an empty prompt prompt_tokens.resize(n_prompt); llama_tokenize(..., prompt_tokens.data(), n_prompt, ...); // return value ignored` `batch_set()` (`~123-133`): `llama_batch_ext_add_token()` return ignored; no check that prompt size <= `n_ctx`/batch capacity. `(int)prompt.toUtf8().size()` truncates a >2 GiB prompt; `prompt.toUtf8()` is recomputed three times; `ContextManager::setUsed()` clamps `n_pos` to capacity, silently masking a prompt that exceeds the context window. `llama_sampler_chain_init()`'s result is never null-checked before `llama_sampler_chain_add()`.
- **Impact:** Negative/huge/NaN config values or an over-long prompt can cause undefined behaviour, crashes, or silent wrong results in native code. Realistically low-probability from the QML path (which uses defaults via `loadModelFile`/`generateText`), higher if `loadModel(cfg)`/`generate(cfg)` are ever exposed.
- **Remediation:** Validate/clamp all config fields (`contextLength` 512–131072, `threads` 1–cores, `topK ≥ 0`, `0 < topP/minP ≤ 1`, `temperature` finite ≥ 0, `maxTokens` 1–context, `seed` handling) at the `LlamaBackend` boundary; use `qint64`/`size_t` for prompt length, check `llama_tokenize` returns, reject prompts whose token count exceeds `n_ctx`, and check batch-add returns and sampler allocation.

### MB-117 — Medium, Confirmed — Engine download: unbounded in-memory body, unchecked write, unsanitised asset name, non-atomic install

- **Sources:** Part 4 M22
- **Files:** `app/inference/EngineInstaller.cpp:154-195`
- **CWE:** CWE-22, CWE-400, CWE-391
- **Evidence:** `m_latestAssetName = m_latestAssetUrl.section('/', -1)` (`:98-101`, `:124-128`) with no sanitisation, then `m_downloadDest = installRoot() + "/" + m_latestAssetName` (`:157`); `onDownloadFinished` does `f.write(reply->readAll())` — the whole archive in RAM, return value ignored (`:~181`), then extracts immediately. No `Content-Length`/size cap, no temp-file + atomic rename; a failed extraction leaves a half-extracted engine dir while `version.txt` is only written on success (`:193-194`), so state is inconsistent.
- **Impact:** Memory exhaustion on a large archive; a `../` or absolute asset name escapes `installRoot`; a disk-full/partial write yields a corrupt engine that `isInstalled()` may still report as installed (existence-only check).
- **Remediation:** Validate the asset name (`^[A-Za-z0-9._-]+$`, no `..`/`/`/`\`), stream to disk with a size cap, check `write()` bytes, extract to a temp dir and rename atomically, and verify the hash first (MB-009).

### 8.4 Build, CI, QML — Medium

### MB-118 — Medium, Confirmed — No compiler/linker hardening flags

- **Sources:** Part 1 F-07
- **Files:** `CMakeLists.txt:20-24`
- **CWE:** CWE-121, CWE-119, CWE-190
- **Evidence:** Only `-Wall -Wextra -Wpedantic` / MSVC `/W4 /permissive-`. No `-fstack-protector-strong`, `-D_FORTIFY_SOURCE=2`, `-fPIE`/`-pie`, `-Wl,-z,RELRO,-z,now`, `-fcf-protection`, MSVC `/GS /guard:cf /DYNAMICBASE /NXCOMPAT`, no PIC, no `-Werror`.
- **Impact:** The binary has none of the standard exploit mitigations, so a memory-safety bug (MB-050, MB-051, MB-114) is easier to turn into code execution.
- **Remediation:** Add the hardening flags for all platforms and enable `-Werror` (or at least fail CI on new warnings).

### MB-119 — Medium, Confirmed — OpenSSL is optional and silently downgrades update verification

- **Sources:** Part 1 F-08
- **Files:** `CMakeLists.txt:153-159`
- **CWE:** CWE-347
- **Evidence:** `find_package(OpenSSL QUIET)`, `HAVE_OPENSSL=1` only if found, with no warning or error.
- **Impact:** A build without OpenSSL silently loses update signature verification (MB-044) — the build succeeds and the app ships with a weaker updater.
- **Remediation:** Make OpenSSL required for release builds, or fail the build with a clear error when it is absent and update verification is enabled.

### MB-120 — Medium, Confirmed — CI actions and package managers are unpinned

- **Sources:** Part 1 F-09
- **Files:** `.github/workflows/build.yml`, `.github/workflows/release.yml`
- **CWE:** CWE-829
- **Evidence:** `actions/checkout@v4`, `setup-python@v5`, `upload-artifact@v4`, `jurplel/install-qt-action@v4`, `softprops/action-gh-release@v2`, `pip install aqtinstall` with no version/hash, `choco install mingw -y` with no version.
- **Impact:** A compromised or changed upstream action/package can alter the build or the released artefacts without any change in this repo.
- **Remediation:** Pin actions to full commit SHAs and pin package versions.

### MB-121 — Medium, Confirmed — Release test step runs the wrong preset and masks failures

- **Sources:** Part 1 F-10
- **Files:** `.github/workflows/release.yml:43-44`
- **CWE:** CWE-1059
- **Evidence:** The job builds `windows-mingw-release`, then runs `ctest --preset windows-mingw-debug || true`. The debug preset was never built in that job. `build.yml` has no test step at all.
- **Impact:** Tests are not actually run on the release build, and failures are masked by `|| true` — a failing test suite ships anyway.
- **Remediation:** Run the release preset, remove `|| true`, fail on test failure, and add a test step to `build.yml`.

### MB-122 — Medium, Confirmed — `scripts/build_macos.sh` calls a test preset that does not exist

- **Sources:** Part 1 F-11
- **Files:** `scripts/build_macos.sh:9`, `CMakePresets.json:73-75`
- **Evidence:** The script calls `ctest --preset "$PRESET"` (default `macos-arm64`), but `CMakePresets.json:73-75` only has a `windows-mingw-debug` test preset.
- **Impact:** The macOS test step fails (or is skipped), so macOS builds are not tested.
- **Remediation:** Add macOS test presets, or call `ctest --test-dir`.

### MB-123 — Medium, Confirmed — macOS packaging signs ad-hoc by default and continues on signing failure

- **Sources:** Part 1 F-12
- **Files:** `scripts/package/macos/package.sh:47-49`
- **CWE:** CWE-347
- **Evidence:** `SIGN_IDENTITY="${SIGN_IDENTITY:--}"`, `codesign ... || echo "codesign failed (continuing)"`, with no `--options runtime`, no `--timestamp`, and no notarisation in this path.
- **Impact:** macOS artefacts can ship unsigned or ad-hoc signed, and a signing failure is silently ignored — users cannot verify the artefact, and Gatekeeper protections are weakened.
- **Remediation:** Require a real signing identity for release packaging, fail on signing failure, and add `--options runtime`, `--timestamp`, and notarisation.

### MB-124 — Medium, Confirmed — Windows timestamp server uses plain HTTP

- **Sources:** Part 1 F-13
- **Files:** `scripts/signing/windows_sign.ps1:11,17,33`, `scripts/signing/signing-config.template.json:5`
- **CWE:** CWE-319
- **Evidence:** `http://timestamp.digicert.com`.
- **Impact:** The timestamp response can be tampered with or observed on the network, weakening the signature's long-term validity.
- **Remediation:** Use `https://timestamp.digicert.com` (or another HTTPS RFC 3161 server).

### MB-125 — Medium, Confirmed — Windows PFX is written to disk in the release job with no cleanup

- **Sources:** Part 1 F-14
- **Files:** `.github/workflows/release.yml:53-58`, `scripts/signing/windows_sign.ps1:33`
- **CWE:** CWE-798, CWE-359
- **Evidence:** The job writes `$env:RUNNER_TEMP\cert.pfx` from a base64 secret, with no deletion, and the password is passed as a CLI argument to signtool (`/p`).
- **Impact:** The signing certificate and its password can leak via process listings, disk images, or runner artefacts.
- **Remediation:** Delete the PFX in a `finally` / `if: always()` step, prefer the certificate store / Key Vault, and pass the password securely (not as a CLI argument).

### MB-126 — Medium, Confirmed — `.gitignore` gaps for secrets and build outputs

- **Sources:** Part 1 F-15
- **Files:** `.gitignore`
- **CWE:** CWE-798, CWE-312
- **Evidence:** Missing `*.pfx`, `*.p12`, `*.pem`, `*.key`, `*.mobileprovision`, `signing-config.json`, `update-manifest.json`, `dist/`, `*.dmg`, `*.zip`, `*.msi`, `.env.*`, `*.crt`, `*.cer`, `credentials*.json`. No secret was found in the current tree.
- **Impact:** A developer can accidentally commit a signing certificate, private key, or environment file.
- **Remediation:** Add the patterns, add pre-commit gitleaks, and add a CI check.

### MB-127 — Medium, Confirmed — Qt version drift: 6.8 vs 6.12

- **Sources:** Part 1 F-16
- **Files:** `CMakeLists.txt:30` (Qt 6.8), `.github/workflows/build.yml` (Qt 6.8.3), `CMakePresets.json` (6.12.0 paths), `.github/workflows/release.yml` (Qt 6.12.0), README/docs (6.12 LTS)
- **Impact:** Builds use different Qt versions depending on the path taken, so behaviour (including security-relevant Qt behaviour such as TLS and redirect handling) differs between builds, and bugs are hard to reproduce.
- **Remediation:** Make the Qt version a single source of truth.

### MB-128 — Medium/Low, Confirmed — `llama.cpp` submodule is empty in the clone, and CMake silently builds without it

- **Sources:** Part 1 F-17
- **Files:** `.gitmodules`, `CMakeLists.txt:33,161-170`
- **CWE:** CWE-829
- **Evidence:** No `else()` error when the submodule is absent; the docs confirm a warning instead of a broken build.
- **Impact:** A build can silently ship without inference at all, or with a stale/different engine than intended.
- **Remediation:** `FATAL_ERROR` when `MODELFIT_ENABLE_LLAMA_CPP=ON` and the submodule is absent.

### MB-129 — Medium/Low, Confirmed — No `permissions:` in `build.yml`; release uses broad `contents: write`

- **Sources:** Part 1 F-18
- **Files:** `.github/workflows/build.yml`, `.github/workflows/release.yml:13-14`
- **CWE:** CWE-732
- **Evidence:** `build.yml` has no `permissions:` block; `release.yml` uses broad `contents: write`.
- **Impact:** A compromised workflow step (MB-120) has broader GitHub token permissions than it needs.
- **Remediation:** `contents: read` for `build.yml`, and least-privilege permissions per job.

### MB-130 — Medium, Suspected — QML: Hugging Face model download URL is built from untrusted strings, with no validation in QML

- **Sources:** Part 1 F-19
- **Files:** `app/ui/ModelsView.qml:258-268`
- **CWE:** CWE-73, CWE-434, CWE-22
- **Evidence:** `url = "https://huggingface.co/" + selectedModelId + "/resolve/main/" + modelData.name`, `fileName = modelData.name.split('/').pop()`, with no allowlist, no `.gguf` check, and `..` not rejected in QML. The C++ downloader was out of scope for the QML reviewer, so this is Suspected at that layer — but the C++ layer is now confirmed vulnerable (MB-010, MB-100), so the QML layer cannot be relied on to compensate.
- **Impact:** A crafted model name/repo can traverse the download destination or fetch an unintended file.
- **Remediation:** Validate in QML **and** C++, allowlist owner/repo, reject `..`/`/`/`\`, require `.gguf`, and verify size/hash (MB-048, MB-100).

### MB-131 — Medium, Suspected — QML: MCP server add form accepts any stdio command / URL, with no approval or validation in QML

- **Sources:** Part 1 F-20
- **Files:** `app/ui/McpHubView.qml:35-48`
- **CWE:** CWE-94, CWE-829
- **Evidence:** The placeholder is `npx -y @modelcontextprotocol/server-filesystem`; the form accepts an arbitrary command/URL, with only a non-empty-name check, and calls `mcpServers.addServerConfig(...)`. The C++ approval was out of scope for the QML reviewer, so this is Suspected at that layer — the C++ layer is confirmed to execute the config with no allowlist (MB-007, MB-148).
- **Impact:** A user pasting (or being tricked into pasting) a command/URL gets arbitrary program execution with no warning.
- **Remediation:** Require explicit confirmation, show the full command/URL, require https-only for URLs, and warn for `npx -y`.

---

## 9. Findings — Low

### 9.1 Server — Low

### MB-132 — Low, Confirmed — Server binds all interfaces despite logging `localhost`

- **Sources:** Server L1
- **Files:** `server/src/index.js:5-12`
- **CWE:** CWE-200, CWE-1188
- **Evidence:** `app.listen(port)` with no host → `0.0.0.0`, while the log says `http://localhost:${port}`. There is no HTTPS enforcement; TLS depends entirely on the reverse proxy in DEPLOY.md.
- **Impact:** The server is reachable on all interfaces (including a mock-mode server, MB-001), while the log suggests it is local-only.
- **Remediation:** Bind `127.0.0.1` for local/dev, the intended interface in production behind a firewall, document the bind address, and add HSTS at the proxy.

### MB-133 — Low, Confirmed — `/health` leaks deployment mode and provider wiring

- **Sources:** Server L2
- **Files:** `server/src/app.js:25-40`
- **CWE:** CWE-200
- **Evidence:** Unauthenticated `{ok:true, service:'modelbench-server', version:'0.1.0', mode: live|mock, ai: …, billing: …}`.
- **Impact:** An attacker can tell whether the server is in mock mode (MB-001) and which providers are wired, without any authentication.
- **Remediation:** Return only `{ok:true}` publicly, and put mode/provider detail behind auth or remove it.

### MB-134 — Low, Confirmed — Unauthenticated, un-rate-limited endpoints enable enumeration/abuse

- **Sources:** Server L3
- **Files:** `server/src/routes/ai.js:40-70` (GET `/ai/models`, POST `/ai/estimate`), `server/src/routes/billing.js:30-35` (GET `/billing/plans`), `server/src/app.js:25-40` (GET `/health`)
- **CWE:** CWE-400, CWE-200
- **Evidence:** `/ai/estimate` accepts caller-supplied `prompt_tokens` (finite numbers only — negative values produce negative `cost_usd`, a minor bug) and is a free pricing oracle. None of these routes has a limiter except `/ai/estimate`'s shared catalogue limiter (240/min/IP).
- **Impact:** Free enumeration and abuse of public endpoints.
- **Remediation:** Clamp `prompt_tokens` to ≥0 with a sane maximum, add limiters to all public routes, and consider requiring auth for `/ai/estimate`.

### MB-135 — Low, Confirmed — Free-model and large-prompt abuse: no per-message content cap, no total-size cap beyond the 2 MB JSON limit

- **Sources:** Server L4
- **Files:** `server/src/routes/ai.js:185-215, 110-130`, `server/src/app.js:20-25`
- **CWE:** CWE-400
- **Evidence:** `validateMessages` caps messages at 200 but there is no per-message/total content length. A free model (`entry.free` → `estimatedCredits=0`) can be fed a ~2 MB prompt at no charge. `DEFAULT_MODEL` is `deepseek-v4.1-flash-free` (see MB-136).
- **Impact:** Free-tier abuse at scale, at the project's upstream cost.
- **Remediation:** Cap total prompt chars/tokens per request (lower for free models) and enforce it before the upstream call.

### MB-136 — Low (functional integrity; security-relevant because it masks testing), Confirmed — `DEFAULT_MODEL` is not in the catalogue — the default chat/estimate path is broken

- **Sources:** Server L5
- **Files:** `server/src/routes/ai.js:35` (`DEFAULT_MODEL='deepseek-v4.1-flash-free'`), `server/src/config/models.js`
- **CWE:** CWE-754
- **Evidence:** There is no entry with that id/slug/alias in the catalogue. The catalogue has `deepseek/deepseek-v4.1-flash` and `xiaomi/mimo-v2.6-flash:free`, but no `deepseek-…-free`. Any `/ai/chat` or `/ai/estimate` without an explicit model fails with `model_not_allowed`. `scripts/smoke.mjs` also uses this model for the free-model 0-credits check, so the smoke test would fail.
- **Impact:** The default path is broken, and the smoke test that would catch billing regressions cannot pass.
- **Remediation:** Set `DEFAULT_MODEL` to a real catalogue slug (for example `mimo-v2.6-flash-free`) and add a startup assertion that the default resolves.

### MB-137 — Low, Suspected — SSE proxy forwards upstream lines verbatim, including event names

- **Sources:** Server L6
- **Files:** `server/src/routes/ai.js:300-360` (`proxyStream`, `consumeLine`)
- **CWE:** CWE-74, CWE-116
- **Evidence:** `if (!line.startsWith('data:')) {write(line)}`; data lines are forwarded verbatim even if `JSON.parse` fails. Only the upstream `[DONE]` is suppressed.
- **Impact:** Upstream (or model output escaping into a raw SSE line) could emit a fake `event: modelbench.usage` with a fabricated balance/`credits_charged` before the server's own final event.
- **Remediation:** Do not forward upstream `event:` lines; only forward `data:` lines that parse as valid chat chunks, and strip/rename upstream events named `modelbench.*`.

### MB-138 — Low, Suspected — Unbounded upstream responses and unvalidated timeout env vars

- **Sources:** Server L7
- **Files:** `server/src/lib/openrouter.js:30-65`, `server/src/lib/whop.js:10-35`
- **CWE:** CWE-400, CWE-20
- **Evidence:** `await upstream.json()` (non-streaming) and `await res.text()` (Whop) have no size cap, while inbound JSON is capped at 2 MB. `Number(process.env.OPENROUTER_TIMEOUT_MS || 180_000)` / `WHOP_TIMEOUT_MS` are not validated — a non-numeric value → `NaN` (immediate/undefined timeout), a huge value → effectively no timeout.
- **Impact:** Memory exhaustion from a large upstream response, and timeouts that do not behave as configured.
- **Remediation:** Cap upstream response size and validate timeout env vars at startup (finite, min/max).

### MB-139 — Low, Confirmed — Password policy is length-only (8 chars), with no maximum, no lockout; tokens are not the only long-lived secret

- **Sources:** Server L8
- **Files:** `server/src/routes/auth.js:30-45`, `server/src/lib/mock.js:55-75`
- **CWE:** CWE-521, CWE-307
- **Evidence:** `if (password.length < minLength)` with `minLength = Number(PASSWORD_MIN_LENGTH || 8)` — no maximum (a multi-MB password is hashed/compared in mock, and passed to Supabase live), no complexity/breached-password check. Live delegates to Supabase Auth; lockout/CAPTCHA are not verifiable from the repo.
- **Impact:** Very long passwords can cause resource exhaustion in mock mode; weak passwords are accepted.
- **Remediation:** Enforce a max length (for example 128 chars), add breached-password screening in live via Supabase, and verify Supabase Auth brute-force protections in the dashboard.

### 9.2 Agent, tools, MCP, skills — Low

### MB-140 — Low (MCP reviewer; grouped here as Medium-Low in the source), Confirmed — MCP: session ID and raw server stderr are logged verbatim; no redaction

- **Sources:** Part 3 V-55
- **Files:** `app/mcp/MCPClient.cpp:366` (`emit log("Mcp-Session-Id: "+m_httpSessionId)`), `:303-304` (`handleStdioStderr`: `emit log(QString::fromUtf8(err).trimmed())` — unbounded, unredacted), `:409-410` (non-JSON HTTP body truncated to 200 — good); `app/mcp/MCPServerManager.cpp:42-52` (logs capped at 1,000 entries, `MaxLogEntries`, `.h:103`)
- **CWE:** CWE-532, CWE-200
- **Impact:** Session hijack if logs are exported/shared; secret leakage via a chatty malicious server.
- **Remediation:** Do not log the session ID (log a truncated hash if needed); redact `Authorization`/`Bearer`/token-like patterns and cap stderr lines (for example 1 KiB, first N lines) before emitting.

### MB-141 — Low, Suspected — stdin is never explicitly closed

- **Sources:** Part 3 V-56
- **Files:** `app/tools/TerminalTool.cpp`, `app/tools/CodeRunner.cpp` (whole files)
- **CWE:** CWE-400
- **Evidence:** No `closeWriteChannel()` call in either file. Suspected because the exact Qt default for an unwritten `QProcess` stdin pipe was not verified.
- **Impact:** A stdin-reading child (`input()`, `read`, `cat`) may block until the 10-minute timeout instead of receiving EOF, holding a slot/resources.
- **Remediation:** Call `closeWriteChannel()` immediately after `start()` when no stdin is to be supplied, or expose an explicit size-capped stdin input.

### MB-142 — Low, Confirmed — `displayCommand()` approval preview can misrepresent the executed argv

- **Sources:** Part 3 V-57
- **Files:** `app/tools/TerminalTool.cpp:62-70,156`
- **Evidence:** Only wraps a part in `"…"` if it contains a space; embedded `"`, `$`, backticks, `\`, and newlines are not escaped. The executed form is an argv list; the shown form is a lossy join.
- **Impact:** A human approving from the preview may misread what will run (for example an argument containing `" + ; …`).
- **Remediation:** Use proper shell quoting/escaping for display, or show argv as a structured list, not a joined string.

### MB-143 — Low, Confirmed — CodeRunner compile stage can exceed the requested timeout

- **Sources:** Part 3 V-58
- **Files:** `app/tools/CodeRunner.cpp:206`
- **CWE:** CWE-400
- **Evidence:** `restartTimer(job, qMax(job->timeoutMs,30000));` — compilation always gets at least 30s even if `timeoutMs=1000`; the total job time can be compile (≥30s) + run (`timeoutMs`).
- **Impact:** Timeouts are not honoured as configured.
- **Remediation:** Honour the requested timeout for compilation (or document/allowlist a separate bounded compile timeout).

### MB-144 — Low/Medium, Confirmed — Git: resource exhaustion — unbounded `git diff --unified`, post-hoc output cap

- **Sources:** Part 3 V-59
- **Files:** `app/tools/GitTools.cpp:220-222` (diff), `:143-159` (`runGit`); contrast log (`:407-408`)
- **CWE:** CWE-400
- **Evidence:** Diff has no bound, unlike log: `argv << "--unified=%1".arg(args.value("context").toInt(3));` / log: `argv << "-n" << QString::number(qBound(1,args.value("count").toInt(20),500));`. `runGit`: `waitForFinished(waitMs)` then `readAll`, with the cap only afterwards: `QByteArray out=process.readAllStandardOutput(); constexpr qint64 kCap=1024*1024; capBytes(&out,kCap,&truncated);`.
- **Impact:** `context=2147483647` makes git generate near-whole-file diffs (CPU/memory); a huge diff in a large repo is buffered unboundedly in `QProcess` before the 1 MiB cap is applied, so the cap does not bound peak memory. Mitigated in part by the 30s default timeout (`:130,143-151`, `kill()` on timeout).
- **Remediation:** `qBound(0,context,100)` (or similar); stream output with `readyRead` + incremental truncation instead of `readAll` after `waitForFinished()`; consider `git diff --stat`/size limits for large repos.

### MB-145 — Low, Suspected — Git: `git_log --author` regex / date filters — option-injection safe, but unbounded regex

- **Sources:** Part 3 V-60
- **Files:** `app/tools/GitTools.cpp:410-415`
- **CWE:** CWE-400
- **Evidence:** `argv << "--author=%1".arg(author); argv << "--since=%1".arg(since); argv << "--until=%1".arg(until);` — the `--opt=value` form correctly prevents a leading-`-` value from becoming a new option.
- **Impact:** `author` is a regex (nested quantifiers) and `since`/`until` are parsed dates; a crafted value can waste CPU up to the 30s timeout. No data-leak/injection was shown.
- **Remediation:** Bound `author` length (for example ≤128 chars), reject regex metacharacters or use exact matching if intended, and validate `since`/`until` as ISO dates before passing them.

### MB-146 — Low, Confirmed (design) — Git: `git commit --all` stages everything, including secrets

- **Sources:** Part 3 V-61
- **Files:** `app/tools/GitTools.cpp:275-281,283-285`
- **CWE:** CWE-200
- **Evidence:** `if(all){runGit({"add","-A"});}` … `argv << "commit" << "-m" << message;` — `-m` as a single argv entry is safe (no shell/option injection from the message, even if it starts with `-`).
- **Impact:** `all=true` stages untracked `.env`/keys/credentials; a subsequent `git_push` (MB-008/MB-029) can then exfiltrate them into history. This is the documented meaning of `all`, so Low — but there is no sensitive-file exclusion or warning.
- **Remediation:** Respect `.gitignore`, warn/block on known secret filenames, or require an explicit allowlist/confirmation for `all=true`; never auto-push after an `all` commit without review.

### MB-147 — Low, Confirmed — MCP: `connectServer` double-connect leaks the old client/process; `removeServer` leaks a QObject

- **Sources:** Part 3 V-63
- **Files:** `app/mcp/MCPServerManager.cpp:214,230,246` (`connectServer` always creates a new `MCPClient(this)` and `m_clients[id]=client` without disconnecting/deleting the previous one), `:177-179` (`removeServer`: `disconnectServer(id); m_clients.remove(id);` — no `deleteLater()`), `:262-278` (`disconnectServer` retains the client; the comment at `:265` says restart reuses it, but `connectServer` never reuses it)
- **CWE:** CWE-401, CWE-772
- **Impact:** Clicking "Connect" twice in `McpHubView` (or calling `connectServerQml` twice) orphans the first client's stdio process (still running, still auto-reconnecting) while the map points at the second. Orphan MCP server processes, duplicate tool traffic, resource leak; a malicious server could survive an apparent disconnect.
- **Remediation:** In `connectServer()`, if `m_clients.contains(id)`, disconnect + `deleteLater()` the old client first; in `removeServer()`, `deleteLater()` after disconnect; fix the stale comment.

### MB-148 — Low, Confirmed — MCP: `addServerConfig` does naive space-splitting and no validation of any field

- **Sources:** Part 3 V-64
- **Files:** `app/mcp/MCPServerManager.cpp:151-160` (`addServerConfig`); `app/mcp/MCPServerManager.h:43-46` (`Q_INVOKABLE`)
- **CWE:** CWE-20
- **Evidence:** `c.args=args.split(' ',Qt::SkipEmptyParts);` — no quote/escape handling. Name/transport/command/url are stored unchecked; there is no duplicate-id/name check in `addServer()` (`:141-149`) — a caller-supplied duplicate id makes `indexOf()`/`removeServer()` ambiguous (first match wins). The QML caller passes the whole command line as `command` with `args=""`, so a placeholder like `npx -y @modelcontextprotocol/server-filesystem` is stored as a single (non-existent) program name and fails to start — a functional bug that also pushes users toward workarounds.
- **Impact:** Malformed server configs, ambiguous identities, and user workarounds that bypass the intended command/args split.
- **Remediation:** Parse the command line with a real shell-word splitter (or separate Command/Args fields in the UI), validate `transport ∈ {stdio, streamable-http}`, reject an empty command/url per transport, and reject duplicate id/name.

### MB-149 — Low, Confirmed — MCP ToolCalling: result routing ignores the JSON-RPC callId; the timeout setter is dead

- **Sources:** Part 3 V-65
- **Files:** `app/mcp/ToolCalling.cpp:183-191,214-217,233-237` (`onClientToolResult(qint64 /*callId*/, …)` — `callId` is explicitly ignored; pending calls are matched FIFO by tool name only: `m_pendingByTool.find(name)/takeFirst()`), `:41-44` (`setDefaultTimeoutMs` stores `m_defaultTimeoutMs`, but `call()` `:135-180` never uses it)
- **CWE:** CWE-20, CWE-362
- **Evidence:** `MCPClient` itself matches responses by id correctly (`MCPClient.cpp:509-518`), but `ToolCalling` discards that id. Two concurrent calls to the same tool (or a server returning results out of order / duplicate) misattribute results to the wrong caller.
- **Impact:** The wrong tool output is fed to the wrong agent step/user — a malicious server can deliberately swap results between two same-name calls.
- **Remediation:** Key pending calls by the `callId` returned from `MCPClient::callTool` (return the id from `callTool`), match on it in `onClientToolResult`/`Error`/`Denied`, and either use `m_defaultTimeoutMs` in `call()` or delete the setter (the 30s per-request timeout in `MCPClient::sendRequest`, `MCPClient.cpp:712-740`, does currently apply).

### MB-150 — Low, Suspected — Approval decisions are bearer tokens by predictable sequential call ID, with no binding to tool/arguments

- **Sources:** Part 3 V-66
- **Files:** `app/tools/ToolRegistry.h:138`; `app/tools/ToolRegistry.cpp:183,330-332`; `app/tools/ToolApproval.cpp:151-173`
- **CWE:** CWE-862, CWE-307
- **Evidence:** `qint64 m_nextCallId=1;` (`.h:138`), `const qint64 id=m_nextCallId++;` (`.cpp:183`); `ToolRegistry::approve(callId,allowed)` and `ToolApproval::decide(callId,approved)` accept any integer, erase the pending entry, and emit `approvalDecided` — without checking who is deciding, or that the caller saw the correct tool/arguments. `autoDecide()` (`ToolApproval.cpp:151-163`) additionally queues `approvalDecided` via `Qt::QueuedConnection` unconditionally, so a decision can still be delivered after the pending entry was cancelled/decided (the registry guards via `m_running`/`awaitingApproval` checks, but other subscribers may not).
- **Impact:** Low in-process (all callers are already in the same trust domain), but a confused-deputy/QML caller that can guess the next sequential ID can pre-approve a future dangerous call, and a stale queued decision can be observed by non-registry listeners.
- **Remediation:** Use unpredictable (random 64-bit) call IDs; bind the decision to `(callId, toolName, args-hash)` and verify it in `decide()`; make `autoDecide()` re-check that the pending entry still exists at delivery time.

### MB-151 — Low, Confirmed — Audit log is capped, clearable, and mode changes are not logged

- **Sources:** Part 3 V-67
- **Files:** `app/tools/ToolApproval.h:84,115`; `app/tools/ToolApproval.cpp:13-17,128-139,250-257`
- **CWE:** CWE-778
- **Evidence:** `m_logLimit=200` with silent eviction (`:138`); `clearApprovalLog()` (`:257`) and `clearPermissions()` (`:250-253`) are `Q_INVOKABLE`; `setMode()` (`:13-17`) changes the gate mode without any log entry.
- **Impact:** Forensic value is weak: an attacker/caller flipping to `AutoApprove` leaves no trace in the log, and the log itself can be wiped or rolled over. Decisions that were auto-approved are logged, but the reason the gate was off is not.
- **Remediation:** Log every `setMode`/permission mutation with a timestamp and caller context; make the log append-only from QML's perspective (remove `Q_INVOKABLE` from `clearApprovalLog`); persist it to disk rather than keeping only the last 200 entries in memory.

### MB-152 — Low, Confirmed — `Application::initialize()` never configures or verifies the approval gate; `initPaths()` ignores failures

- **Sources:** Part 3 V-68
- **Files:** `app/Application.cpp:22-45,51-53,63-66`
- **CWE:** CWE-754, CWE-862
- **Evidence:** `m_agent=new AgentOrchestrator(nullptr,nullptr,this);` (`:37`) — null registry **and** null approval (the `AgentOrchestrator` then creates its own `ToolRegistry`/`ToolApproval` internally). `QDir().mkpath(m_dataDir); mkpath(m_modelsDir); mkpath(m_logsDir);` (`:51-53`) — return values ignored. `Database db(m_dataDir+"/modelfit.db");` (`:64`) — string concatenation, no path validation.
- **Impact:** Nothing in the audited files asserts that the agent's registry is actually wired to a Prompt-mode gate, that tools are registered, or that the UI's `approve()` path is connected — a wiring regression silently yields either a default-constructed gate or no gate. Ignored `mkpath` failures mean the app can proceed with missing/unwritable data/log dirs (and a missing audit trail).
- **Remediation:** Construct `ToolRegistry`/`ToolApproval` explicitly in `Application`, set mode Prompt, register tools, and fail startup (or fail closed) if any step fails; check `mkpath` results and log/fail on error.

### MB-153 — Low, Confirmed — Filesystem: `list_dir` leaks metadata for symlinked entries outside the root

- **Sources:** Part 3 V-69
- **Files:** `app/tools/FileSystemTools.cpp:431-434,449-472`
- **CWE:** CWE-200, CWE-59
- **Evidence:** Recursive listing does not follow symlinked directories (no `FollowSymlinks` — safe by default), but symlink files/dirs are still returned as entries with the target's size/modified time and a lexical in-root path. That alone only leaks existence/size/mtime, and feeding such a path to `read_file` is blocked by its canonical check — but it hands the model exact outside targets to probe via MB-031.
- **Impact:** Information disclosure that enables the MB-031 bypass.
- **Remediation:** Skip `isSymLink()` entries, or return only the canonical path after an inside-root check.

### MB-154 — Low, Confirmed — SystemIntegration: clipboard broadcast and notification spoofing

- **Sources:** Part 3 V-70
- **Files:** `app/tools/SystemIntegration.cpp:20-35,65-72`; `app/tools/SystemIntegration.h:14,46`
- **CWE:** CWE-200, CWE-451
- **Evidence:** `Clipboard::text()` exposes the full system clipboard to QML, and `onSystemClipboardChanged()` (`:35`) emits `textChanged(text())` on **every** clipboard change, broadcasting passwords/secrets the user copied to any QML binding. `setText()` has no size/approval gate. `Notifier::notify()` has no origin validation, so any QML caller can post a spoofed "ModelBench" notification. Mitigating: neither class is an agent Tool (not in `ToolRegistry`), so the LLM cannot call directly; only QML/UI can. The duration bound (`qBound(1000,durationMs,30000)`, `:71`) is good.
- **Impact:** Clipboard contents (often passwords/secrets) are broadcast to QML bindings; notifications can be spoofed.
- **Remediation:** Do not emit clipboard content on change (emit a bare signal); require an explicit user action for clipboard reads; length-cap titles/messages and prefix notifications with their real source.

### MB-155 — Low/Medium, Confirmed by design; exposure Suspected — Agent: `exportRun()` and step signals expose full memory and tool arguments with no redaction

- **Sources:** Part 3 V-71
- **Files:** `app/agent/AgentOrchestrator.cpp:98,135,310-317` (`exportRun`); `app/agent/AgentMemory.cpp:188-199` (`toJson`)
- **CWE:** CWE-200
- **Evidence:** `o["steps"]=m_planner.toJson(); o["log"]=m_runLog; o["memory"]=m_memory.toJson(); o["edits"]=m_history?m_history->journal():QJsonArray();` — `agentStepStarted(index,description,toolName,args)` and `approvalRequired(...,args,preview,...)` forward full args; the run-log observation is capped at 2000 chars but not redacted. No secret logging was found in scope (`grep qDebug/password/secret` found only the SkillsShClient token header, MB-036). `ToolApproval::makePreview` redacts secrets — the orchestrator's own signals/export do not.
- **Impact:** Facts/project values the user asked the agent to remember (passwords, API keys, addresses) are included verbatim in any export/share of a run. No caller of `exportRun()` was found, so exposure depends on future use.
- **Remediation:** Redact secret-looking keys/values in `exportRun()`, the run-log, and step/approval signals (reuse the `ToolApproval::makePreview` logic); warn before exporting memory; offer a memory-clear / secret-scrub path.

### MB-156 — Low/Medium, Suspected — Agent Planner: passes raw, unsanitised user text as tool arguments (path / branch / command / message)

- **Sources:** Part 3 V-72
- **Files:** `app/agent/AgentPlanner.cpp:10-41` (`tailAfter`/`quotedText`/`firstPath`), `:151` (`firstPath`), `:166` (commit message), `:173-182` (branch checkout name), `:205-214` (write path/content), `:230-242` (terminal command + `shell:true`)
- **CWE:** CWE-88, CWE-78
- **Evidence:** Path, branch, message, query, and command are all substrings of the raw goal, with no allowlisting, escaping, or `--` separator in the planner itself; a branch name starting with `-` or a command containing `;`/`|`/`$()` is forwarded as-is.
- **Impact:** Argument/option injection into `git_branch` / `run_terminal` / file tools if a downstream tool builds a shell/CLI invocation without its own safeguards. The downstream tools were **not** in this section's scope and not verified — therefore Suspected, not Confirmed, at the planner layer. (Cross-reference: the downstream git/terminal findings MB-004, MB-018, MB-028 confirm that downstream safeguards are in fact weak.)
- **Remediation:** Validate paths/branch names against strict patterns in the planner; pass `--` before user-supplied git operands; never enable `shell:true` from goal text; rely on (and verify) downstream allowlists rather than assuming them.

### 9.3 Core, storage, models, inference, hardware, chat — Low

### MB-157 — Low, Confirmed stub — `LocalApiServer::start()` is a stub that claims to be running; future wiring has no auth/TLS design

- **Sources:** Part 4 L1
- **Files:** `app/api/LocalApiServer.cpp:10-16`; `app/api/LocalApiServer.h:12`
- **CWE:** CWE-693, CWE-862
- **Evidence:** `bool LocalApiServer::start(const QString &host, quint16 port) { m_endpoint = QString("http://%1:%2/v1").arg(host).arg(port); m_running = true; emit started(m_endpoint); return true; // no QTcpServer::listen() at all }` No socket is ever bound (despite `QTcpServer`/`QTcpSocket` includes), so **there is currently no exposed port and no auth bypass**. The danger is the false signal: callers/UI believe an OpenAI-compatible API is live. The default is `127.0.0.1:8080`, but `start()` accepts any host including `0.0.0.0`, with no auth token, TLS, or origin check specified.
- **Impact:** Currently none at runtime; high future risk if the handler is wired as-is.
- **Remediation:** Either remove the stub or, when implementing, bind loopback only by default, require a per-launch random Bearer token, and never claim `started` until `listen()` succeeds.

### MB-158 — Low, Confirmed — `SubscriptionManager`: client-side-only gating, stub entitlements, unchecked enum cast, empty checkout URL

- **Sources:** Part 4 L2
- **Files:** `app/billing/SubscriptionManager.cpp:16-34`; `app/billing/SubscriptionManager.h:14, 20-21`
- **CWE:** CWE-862, CWE-704
- **Evidence:** `bool SubscriptionManager::isFeatureEnabled(const QString &feature) const { if (m_tier == Free) return feature == "inference" || feature == "chat" || feature == "models"; return true; // any feature string, any paid tier }` `void SubscriptionManager::refreshEntitlements() { /* no-op stub */ }` `void SubscriptionManager::startCheckoutTier(int tier) { startCheckout(static_cast<Tier>(tier)); }` — no range check. `void SubscriptionManager::startCheckout(Tier tier) { Q_UNUSED(tier); emit checkoutReady(QString()); }` `m_tier` defaults to `Free`, is never set in-scope, and `tierChanged` is never emitted. Paid features are therefore gated only by this client-side check, which must not be trusted for enforcement.
- **Impact:** Trivial local bypass of feature gating if QML/server trusts it; an out-of-range `tier` from QML yields an invalid enum; an empty `checkoutReady("")` URL may be opened by the UI.
- **Remediation:** Validate `tier` in `[Free, Teams]` before casting; make `startCheckout()` actually call `WhopClient` or remove it; treat `isFeatureEnabled()` as a UI hint only and enforce entitlements server-side (via `AccountManager::refreshEntitlements()`).

### MB-159 — Low, Confirmed pattern; filename-traversal Suspected — `Updater::installUpdate()` / `downloadUpdate()`: unchecked writes, no extension/path check, stale state on cancel

- **Sources:** Part 4 L3
- **Files:** `app/core/Updater.cpp:198-199, 233-241, 347-361, 372-384`
- **CWE:** CWE-73, CWE-252
- **Evidence:** `QUrl(m_downloadUrl).fileName()` (`:198`) is used unsanitised (no `PathSecurity::sanitizeFileName()` / `isInside()` check). `QUrl::fileName()` normally returns only the last segment, so classic `../` traversal is **not proven** — it needs a runtime test with percent-encoded separators — but defence-in-depth demands sanitising it anyway. `f.write()` / `f.close()` return values are ignored (`:240-241`); a truncated file proceeds to verification (fail-closed by SHA/signature, so Low). `cancel()` (`:372-384`) only removes the download when the state is `Downloading`; a `Verifying`/`Ready` file is left in `/tmp` indefinitely. `compareVersions()` (`:81-92`): an unused `QRegularExpression re`, `QString::toInt()` silently yields `0` for non-numeric/overflowing components — weak parsing, no downgrade/rollback policy (mitigated: an older manifest version simply yields `Idle`, no download).
- **Impact:** Stale update files left in temp; weak version parsing; a defence-in-depth gap on the filename.
- **Remediation:** Sanitise/allowlist the filename, check I/O results, delete the temp file on `cancel()` in all non-`Idle` states, validate version strings strictly (`^\d+\.\d+\.\d+$`), and consider a minimum-version/rollback guard.

### MB-160 — Low, Confirmed — `Logging::install()`: log file with default permissions, no rotation, `setLevel()` is a no-op

- **Sources:** Part 4 L4
- **Files:** `app/core/Logging.cpp:44-51, 25-42`
- **CWE:** CWE-276, CWE-400
- **Evidence:** `void install(const QString &logDir) { QDir().mkpath(logDir); logFile = new QFile(logDir + "/modelfit.log"); logFile->open(QIODevice::Append | QIODevice::Text); // return value ignored, no setPermissions(0600) qInstallMessageHandler(handler); }` `void setLevel(int level) { Q_UNUSED(level); }` The handler also builds a `QTextStream` per message with no locking (interleaving under concurrent threads — minor), and calls `abort()` on `QtFatalMsg` (`:41`), turning any `qFatal()` (including from a library) into an immediate process abort with no cleanup.
- **Impact:** The log (which may contain paths/emails/errors — see MB-108) is world-readable under the default umask on multi-user machines; unbounded append can fill the disk; debug logging cannot be disabled in production; a fatal log kills the app.
- **Remediation:** Check the `open()` result, `QFile::setPermissions(0600)`, add size-based rotation, implement `setLevel()` (filter in `handler()`), guard writes with a mutex, and consider logging + graceful shutdown instead of `abort()`.

### MB-161 — Low, Confirmed — No timeouts / HTTP-status checks on network calls in scope

- **Sources:** Part 4 L5
- **Files:** `app/core/AccountManager.cpp:20-33, 72-86`; `app/billing/WhopClient.cpp:21-41, 48-61`; `app/core/Updater.cpp:104-113, 201-210`; `app/models/HuggingFaceClient.cpp:33-56, 61-81`; `app/inference/EngineInstaller.cpp:56-113, 169-183`
- **CWE:** CWE-400, CWE-754
- **Evidence:** No `QNetworkRequest` in scope sets a timeout (`grep transferTimeout|setTimeout` → none), and `AccountManager`/`WhopClient` treat any reply without a `QNetworkReply` error as success regardless of HTTP status (they parse JSON unconditionally). `WhopClient` also allocates a new `QNetworkAccessManager` per call (`:21, 48`) instead of reusing one. Every network read in models/inference is `reply->readAll()` + `QJsonDocument::fromJson(...)` with no size cap.
- **Impact:** Hung UI/network threads on a stalled server; ambiguous error handling; a malicious/compromised server or a huge repo listing can exhaust memory.
- **Remediation:** Set a transfer timeout (for example 30 s for APIs, longer for downloads), check `reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)`, cap `readAll()`/JSON size, stream large downloads to disk, and reuse a single `QNetworkAccessManager`.

### MB-162 — Low, Confirmed — `VectorStore`: main-table + FTS writes are non-atomic; FTS errors ignored

- **Sources:** Part 4 L6
- **Files:** `app/storage/VectorStore.cpp:92-135, 137-175`
- **CWE:** CWE-362, CWE-703
- **Evidence:** `upsert()` (`:101-132`) does `INSERT ... ON CONFLICT` then a separate `DELETE` + `INSERT` into `vector_entries_fts`; `remove()` (`:137-155`) and `removeByNamespace()` (`:157-175`) similarly. No `BEGIN`/`COMMIT` transaction; FTS `del.exec()`/`ins.exec()`/`f.exec()` return values are ignored (`:122, :131, :152, :172`).
- **Impact:** A crash/failure between statements leaves the FTS index diverged from the main table — deleted entries remain searchable, upserted entries unsearchable (stale-data / information-disclosure of supposedly-deleted content).
- **Remediation:** Wrap each main+FTS pair in a transaction and check/rollback on any failure.

### MB-163 — Low, Confirmed — Database: migrations unchecked and non-atomic; the version is bumped regardless

- **Sources:** Part 4 L7
- **Files:** `app/storage/Database.cpp:24-25, 29-48`
- **CWE:** CWE-703
- **Evidence:** `:24-25` PRAGMA results ignored; `:32` `INSERT OR IGNORE` ignored; `:40-43` `t.exec("CREATE TABLE ...")` results ignored; `:44` `t.exec("UPDATE schema_version SET version=1;")` unconditional; `:47` `qInfo() << "Database migrated to version" << version; return true;`
- **Impact:** If a `CREATE TABLE` fails (for example a corrupt/locked DB), the app still reports success and marks version 1, then fails later in unrelated code with confusing errors.
- **Remediation:** Check every `exec()`, log `lastError()`, wrap the migration in a transaction, and only bump `schema_version` after all statements succeed.

### MB-164 — Low, Confirmed — `CodeIndexer`: TOCTOU between the size/binary probe and the re-read

- **Sources:** Part 4 L8
- **Files:** `app/storage/CodeIndexer.cpp:316-330` vs `:153-165`
- **CWE:** CWE-367
- **Evidence:** `indexProject()` checks `fi.size() > maxFileBytes` (`:316`), opens a `probe` and reads 4096 bytes for `isLikelyBinary()` (`:321-328`), then `chunkFile()` re-opens the same path and re-reads `maxFileBytes+1` (`:157-163`).
- **Impact:** A file swapped (or symlink swapped) between the probe and the chunk can bypass the large/binary checks; low severity — local, requires a concurrent writer to the indexed tree.
- **Remediation:** Open once, `fstat` the open handle for size, read once, and pass the bytes to the chunker.

### MB-165 — Low, Confirmed missing validation; Suspected overflow trigger — `CodeIndexer` / `VectorStore`: unvalidated caller-controlled sizes; `maxFileBytes+1` overflow

- **Sources:** Part 4 L9
- **Files:** `app/storage/CodeIndexer.cpp:153-179, 26-29` (`IndexOptions` in `CodeIndexer.h:25-32`); `app/storage/VectorStore.cpp:514-530`
- **CWE:** CWE-400, CWE-190, CWE-770
- **Evidence:** `chunkFile()` clamps `chunkLines`/`overlap` safely (`:172-174`) but never validates `maxFileBytes` or `maxChunksPerFile`; `f.read(options.maxFileBytes + 1)` (`:161`) overflows if `maxFileBytes == INT_MAX`. `indexProject()`/`fileTree()` have no total-file/chunk/byte cap. `vectorToBlob()` (`:518`) does `blob.resize(static_cast<int>(vec.size() * sizeof(float)))` (int multiplication); `blobToVector()` (`:523-530`) allocates `QVector<float>(blob.size()/4)` from an untrusted DB blob with no max-dimension check and ignores the stored `dim` column.
- **Impact:** A caller passing huge `IndexOptions`, or a tampered/corrupt DB with a giant embedding blob, can cause large allocations / memory exhaustion. Realistic vectors are far below the overflow threshold, so this is Low.
- **Remediation:** Hard-clamp `maxFileBytes` (for example `1..2*1024*1024`), cap total files/chunks, use `qint64`/`size_t` arithmetic, enforce a max embedding dimension (for example 8192) and verify `blob.size() == dim * sizeof(float)`.

### MB-166 — Low, Confirmed missing synchronisation; Suspected concurrent use — Shared `QSqlDatabase` connection, no locking; mutable FTS cache race

- **Sources:** Part 4 L10
- **Files:** `app/storage/Database.cpp:17`, `app/storage/Database.h:12-14`, `app/storage/Repositories.cpp:11, 20`, `app/storage/VectorStore.h:78-79`, `app/storage/VectorStore.cpp:73-90`
- **CWE:** CWE-362
- **Evidence:** The fixed connection name `"modelfit"` is shared by `Database`, `Repositories` (`QSqlDatabase::database("modelfit")`), and `VectorStore` (fallback `:44-45`); `Database::~Database()` closes but never calls `removeDatabase()` (`:12-14`); `VectorStore::ensureFts()` mutates `mutable bool m_ftsChecked/m_ftsAvailable` (`:73-76`) with no mutex, and no mutex exists anywhere in scope (`grep` for `QMutex|transaction`: none). Qt connections are not thread-safe / have thread affinity.
- **Impact:** Concurrent calls from different threads can race the FTS cache and violate `QSqlDatabase` thread affinity, causing crashes or corrupt queries. Whether the app actually calls these from multiple threads is out of scope — Suspected.
- **Remediation:** One connection per thread (or a single-threaded DB executor + `QMutex`), `removeDatabase()` in the destructor, and `PRAGMA busy_timeout=5000` (currently absent) for WAL contention.

### MB-167 — Low, Confirmed — `Repositories`: no key allowlist, silent failure masks insecure defaults

- **Sources:** Part 4 L11
- **Files:** `app/storage/Repositories.cpp:10-26`
- **CWE:** CWE-703, CWE-1188
- **Evidence:** `setSetting()` accepts any `key`/`value` with no allowlist or length limit and silently overwrites via `INSERT OR REPLACE` (`:13-16`); `getSetting()` does `QSqlDatabase::database("modelfit")` without checking `isOpen()` and silently returns `defaultValue` on any failure (`:20-25`).
- **Impact:** Any caller can overwrite any setting (including security-relevant ones, if such keys exist — not verified in scope); a DB failure silently yields the caller's default, which may be less secure than the stored value. Unbounded values also allow DB bloat.
- **Remediation:** Allowlist setting keys, cap value length, check `db.isOpen()` and log `lastError()` on failure instead of silently returning the default.

### MB-168 — Low, Confirmed — No authorisation / IDOR within the app: any QML caller can enumerate, read, and delete every conversation

- **Sources:** Part 4 L12
- **Files:** `app/chat/ConversationManager.cpp:34-44, 46-57, 64-78`; `app/chat/ConversationManager.h:22-28`
- **CWE:** CWE-862
- **Evidence:** `getConversations()` returns all ids/titles, `conversationIds()` enumerates every id (`:69-73`), and `getMessages(convId)` and `deleteConversation(convId)` perform no ownership check. IDs are `QUuid::createUuid()` (`:14, 24`) so unguessable — but the enumeration APIs hand the full list to any caller, defeating that.
- **Impact:** Limited in a single-user desktop app (an attacker must already run QML/JS in the app), but no defence-in-depth: a compromised plugin/script can dump or wipe all chat history.
- **Remediation:** If conversations ever become per-user/per-profile/synced, add an owner/principal check to every accessor; otherwise document the single-user trust boundary and avoid exposing `conversationIds()` to untrusted QML.

### MB-169 — Low, Confirmed — Unbounded in-memory conversation/message growth (memory-exhaustion DoS)

- **Sources:** Part 4 L13
- **Files:** `app/chat/ConversationManager.cpp:12-20, 22-32`; `app/chat/ConversationManager.h:34-35`
- **CWE:** CWE-400
- **Evidence:** `QList<Conv> m_convs;` with no cap; `createConversation()` appends, `addMessage()` appends, with no maximum on conversations, messages per conversation, or length of `title`/`modelId`/`content`; no eviction. Data is in-memory only, so a restart clears it.
- **Impact:** A loop in QML/automation (or a pasted huge message) grows process memory without bound.
- **Remediation:** Enforce caps (max conversations, max messages per conversation, max message/title length — truncate or reject) and enforce them at the storage layer too if persistence is added.

### MB-170 — Low, Suspected (no cross-thread caller found in scope; all current QML calls are on the main thread) — `ConversationManager` has no thread-safety for its shared mutable state

- **Sources:** Part 4 L14
- **Files:** `app/chat/ConversationManager.cpp:12-32, 75-78`; `app/chat/ConversationManager.h:34-35`
- **CWE:** CWE-362
- **Evidence:** `QList<Conv> m_convs` is mutated by `createConversation()`/`addMessage()`/`deleteConversation()` and read by `const` getters, with no `QMutex`, no `QReadWriteLock`, and no thread-affinity assertion; the methods are `Q_INVOKABLE`.
- **Impact:** If a worker thread ever calls these (for example a future async inference callback adding messages), concurrent `QList` mutation/iteration is a data race / use-after-free / crash.
- **Remediation:** Guard `m_convs` with a `QMutex`/`QReadWriteLock`, or confine all access to the object's thread via queued connections and assert thread affinity.

### MB-171 — Low, Confirmed — `runProcess()` ignores the exit code / failure mode and parses stdout unconditionally

- **Sources:** Part 4 L15
- **Files:** `app/hardware/HardwareDetector.cpp:30-40`
- **CWE:** CWE-754, CWE-20
- **Evidence:** `QProcess process; process.start(program, arguments); if (!process.waitForFinished(timeoutMs)) { process.kill(); process.waitForFinished(500); return {}; } return QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();` — no check of `exitCode()` / `exitStatus()`; stderr is discarded. The 3 s default timeout (8 s for `system_profiler`, line 319) and `kill()` are good mitigations against hangs.
- **Impact:** A helper that exits non-zero, crashes, or prints an error/warning to stdout has its output parsed as hardware data (for example `detectNvidiaSmi()`, lines 143-162, splits CSV blindly). Combined with MB-113, a fake helper's output is trusted. On its own, the impact is limited to wrong GPU/RAM reporting.
- **Remediation:** Return empty unless `exitStatus() == QProcess::NormalExit && exitCode() == 0`; validate the parsed shape (expected field count / numeric ranges) before using it.

### MB-172 — Low, Confirmed code pattern; Suspected as exploitable (requires malicious output from a normally-trusted helper / kernel file) — Integer wrap / out-of-range conversion in VRAM / RAM size parsing

- **Sources:** Part 4 L16
- **Files:** `app/hardware/HardwareDetector.cpp:42-60, 152-157, 282`
- **CWE:** CWE-190, CWE-191
- **Evidence:** `parseMemorySize()`, lines 53-59: `return static_cast<quint64>(value * 1024.0 * 1024.0 * 1024.0);` — double → `quint64`, UB if out of range. `detectNvidiaSmi()`, line 156: `info.vramBytes = mib * 1024ULL * 1024ULL;` — `mib` is `quint64` from `toULongLong()`; wraps mod 2^64. `detectLinux()`, line 282: `info.ramBytes = number.toULongLong() * 1024ULL;` — same wrap. `mib`/`number` are not range-checked before multiplication; `toULongLong(&ok)` at line 153 only checks parse success, not magnitude.
- **Impact:** A huge value wraps to a small/incorrect VRAM/RAM figure, which could cause the app to pick a wrong backend, underestimate memory, or misreport hardware. Realistic sources (`/proc/meminfo`, genuine `nvidia-smi`/`system_profiler`) are kernel/system-trusted, so this is defence-in-depth rather than a live exploit path.
- **Remediation:** Check `mib > UINT64_MAX / (1024*1024)` (and the equivalent for KiB) before multiplying and clamp/reject on overflow; in `parseMemorySize()`, reject non-finite or out-of-range `double` values before the `static_cast<quint64>`.

### MB-173 — Low, Confirmed — Information disclosure: full library path and driver strings exposed via `toJson()`

- **Sources:** Part 4 L17
- **Files:** `app/hardware/BackendDetector.cpp:277-293` (line 285: `item["libraryPath"] = backend.libraryPath;`); `app/hardware/HardwareDetector.cpp:392-414` (lines 400, 403-404: `ramBytes`/`vramBytes`/`gpuDriver` as `double`/string)
- **CWE:** CWE-200
- **Evidence:** `BackendDetector::toJson()` emits the absolute `libraryPath` (for example the install directory layout); `HardwareDetector::toJson()` emits CPU/GPU model, driver version, and byte counts. The `toJson()` output is returned to QML via `Application::hardwareInfo()`.
- **Impact:** Low locally — it aids fingerprinting and reveals install paths/driver versions. It becomes material only if this JSON is ever sent to a server, logged, or included in telemetry/crash reports.
- **Remediation:** Omit `libraryPath` from any JSON that leaves the process or is logged; expose only backend `name`/`available`/`gpu` to the UI. Audit any future telemetry/crash-report path for these fields. (Separate precision note: `ramBytes`/`vramBytes` are cast to `double` at lines 400/403, losing exactness above 2^53 — cosmetic, not a security issue.)

### MB-174 — Low, Suspected — `uninstall()` / `isInstalled()` / `installedVersion()` trust the engine directory without integrity checks

- **Sources:** Part 4 L18
- **Files:** `app/inference/EngineInstaller.cpp:28-46, 222-225`
- **CWE:** CWE-59, CWE-367
- **Evidence:** `uninstall() { QDir(installRoot()).removeRecursively(); }` — if `installRoot()` (`AppDataLocation/engine`) were a pre-planted symlink, `removeRecursively()` follows it; `installedVersion()` returns unvalidated `version.txt` contents for display; `isInstalled()` is existence-only.
- **Impact:** A local attacker (or a prior malicious archive, MB-042) that plants a symlink could cause deletion of files outside the engine dir on uninstall; a tampered `version.txt`/DLL is reported as a valid install.
- **Remediation:** Refuse to uninstall if `installRoot()` is a symlink (check `QFileInfo::isSymLink()`), validate `version.txt` format, and verify the installed library hash before reporting installed (MB-009).

### MB-175 — Low, Confirmed — `ModelRegistry` / `ModelDownloads` bookkeeping gaps (no security boundary, but worth noting)

- **Sources:** Part 4 L19
- **Files:** `app/models/ModelRegistry.cpp:5-14`; `app/models/ModelDownloads.cpp:8-14`
- **CWE:** CWE-59, CWE-20
- **Evidence:** `add()` has no dedup and no path validation; `find()` silently returns a default-constructed model on a miss; `listDownloadedModels()` lists top-level `*.gguf` only and follows symlinks. The registry is in-memory only (no persistence found in scope), so the impact is confined to the running app.
- **Impact:** Confined to the running app; no security boundary is crossed on its own.
- **Remediation:** Validate paths on `add()` (inside the models dir, `.gguf`), dedup by id, and resolve symlinks/canonical paths when listing.

### MB-176 — Low, Confirmed — `ContextManager::addTokens()` signed-integer overflow before clamping; non-atomic compound updates

- **Sources:** Part 4 L20
- **Files:** `app/inference/ContextManager.cpp:6-30`
- **CWE:** CWE-190, CWE-362
- **Evidence:** `void ContextManager::addTokens(int tokens) { setUsed(m_used.load(...) + tokens); }` (`:27-30`) — the addition overflows before `setUsed()` clamps. `void ContextManager::setCapacity(int capacity) { m_capacity.store(std::max(0, capacity), ...); const int cap = m_capacity.load(...); int used = m_used.load(...); if (used > cap) used = cap; m_used.store(...); }` (`:6-15`) — separate relaxed loads/stores, with a lost update under concurrency.
- **Impact:** `addTokens(INT_MAX)` is signed overflow (UB); concurrent `setCapacity`/`setUsed` can leave an inconsistent used/capacity pair. Counters are token counts, so realistic exploitation is negligible — but the class is explicitly documented as cross-thread (`ContextManager.h`).
- **Remediation:** Use `qint64` internally or saturating addition (`std::clamp` on `int64_t`); make compound updates atomic (a single mutex or a `compare_exchange` loop).

### MB-177 — Low, Confirmed absence; severity Low — No file-permission hardening; the token is held as a plaintext `QString`

- **Sources:** Part 4 L21
- **Files:** `app/models/HuggingFaceClient.cpp:14-22, 28`; `app/models/HuggingFaceClient.h:28`; `grep setPermissions|chmod` in scope → none
- **CWE:** CWE-256, CWE-276
- **Evidence:** `m_token` is a plain `QString` member, set via QML (`SettingsView.qml:45` — "Token set for this session", and `grep` confirms no disk persistence of the token anywhere in the repo — good). It is sent as `Authorization: Bearer …` on every API request (`:20`). No `QFile::setPermissions()` is used for downloaded models, the engine library, or `version.txt` — all rely on umask defaults (typically 0644/0755).
- **Impact:** The token is recoverable from a memory dump / swap, and is exposed to any code that can read the process; model/engine files are world-readable on a shared machine. No disk leak of the token was found. Qt offers no secure-string type, so this is largely inherent — note as defence-in-depth, not a defect.
- **Remediation:** Clear the token on demand / on lock, avoid logging it (already the case), consider OS keychain storage if persistence is ever added; set restrictive permissions (`0600` for any future credential files, `0644` explicitly for models) and document the choice.

### MB-178 — Low, Confirmed — `PathSecurity::sanitizeFileName()` is a denylist, not an allowlist

- **Sources:** Part 4 L22 (kept separate from MB-101, which covers `isInside()` itself; the two functions share a file but are distinct controls)
- **Files:** `app/core/PathSecurity.cpp:17-23`
- **CWE:** CWE-73
- **Evidence:** `out.replace('/', '_'); out.replace('\\', '_'); out.replace("..", "_");` It does not strip NUL/control characters, `:`, leading/trailing dots/spaces, or Windows reserved names (`CON`, `NUL`, `COM1`). It does neutralise `/`, `\`, and `..` for traversal, so it is not currently exploitable for traversal on its own — but it is insufficient as a general filename sanitiser.
- **Impact:** Insufficient as a general sanitiser; defence-in-depth gap.
- **Remediation:** Allowlist `[A-Za-z0-9._-]` (reject/empty-check the result), strip leading dots, and reject reserved Windows names. See MB-101.

### 9.4 Build, CI, QML, docs — Low

### MB-179 — Low/Medium, Confirmed — AgentTimeline shows full tool arguments in plaintext

- **Sources:** Part 1 F-22
- **Files:** `app/ui/AgentTimeline.qml:115-125`
- **CWE:** CWE-200
- **Evidence:** `JSON.stringify(modelData.args, null, 2)` with no masking. The C++ test does redact (`test_tool_registry.cpp:235-238`), but that redaction is not applied in this QML view.
- **Impact:** Secrets in tool arguments are displayed in plaintext in the timeline UI. See also MB-025 (approval signals) and MB-155 (export).
- **Remediation:** Apply the same redaction as the C++ test to this view.

### MB-180 — Low, Confirmed — `docs/RELEASE.md:30` contains leaked agent-harness markup appended after the manifest-template line

- **Sources:** Part 1 F-24
- **Files:** `docs/RELEASE.md:30`
- **CWE:** CWE-829, CWE-116
- **Evidence:** Trailing agent-harness markup is appended after the manifest-template line.
- **Impact:** Leaked internal tooling markup in shipped documentation; a sign that agent-generated docs are not reviewed before commit.
- **Remediation:** Remove the trailing markup, review agent-generated docs, and add a CI check for harness tokens.

### MB-181 — Low/Medium, Confirmed — No vulnerability-disclosure policy

- **Sources:** Part 1 F-25
- **Files:** `docs/SECURITY.md:1-50`
- **CWE:** CWE-1059
- **Evidence:** `docs/SECURITY.md` lists issues but gives no contact, reporting channel, timeline, scope, or safe-harbour. There is no root `.github/SECURITY.md` / `security.txt`.
- **Impact:** A researcher who finds a vulnerability in ModelBench has no safe, documented way to report it — so reports may go public, or not arrive at all. See §15.
- **Remediation:** Add a disclosure policy (contact, scope, timeline, safe harbour), a root `.github/SECURITY.md`, and a `security.txt`.

### MB-182 — Low, Confirmed — Installer requires admin and uninstall does a recursive delete

- **Sources:** Part 1 F-26
- **Files:** `scripts/package/windows/ModelBench.nsi:7,23`, `scripts/package/windows/package.ps1:67-80`
- **CWE:** CWE-276, CWE-732
- **Evidence:** `RequestExecutionLevel admin`, `RMDir /r "$INSTDIR"`.
- **Impact:** The installer runs with admin privileges and the uninstaller recursively deletes the install directory — a larger blast radius than a per-user install, and a risk if `$INSTDIR` is ever wrong (compare MB-174 for the engine uninstaller).
- **Remediation:** Per-user install, and delete only installer-written files.

### MB-183 — Low/Medium, Confirmed — `verify.mjs` is broken at the repo root

- **Sources:** Part 1 F-28
- **Files:** `verify.mjs`
- **Evidence:** It imports `./env.js` and `./src/config/pricing.js`, which do not exist at the root (the real files are under `server/src/`). The webhook check is only `typeof whop.verifyWebhookSignature` and a string search for `timingSafeEqual`.
- **Impact:** The verification script that is supposed to check the billing setup cannot run, and even if it could, it would not actually verify a webhook signature.
- **Remediation:** Fix the paths to `server/…`, exit non-zero on failure, run it in CI, and use a real HMAC test.

---

## 10. Findings — Info

### MB-184 — Info, Confirmed — Dead/misleading workflow input and version drift

- **Sources:** Part 1 F-27
- **Files:** `.github/workflows/release.yml:7-11`, `CMakeLists.txt:10`, `scripts/package/windows/ModelBench.nsi`, `scripts/package/windows/package.ps1`
- **Evidence:** The `workflow_dispatch` input `version` is never used; `CMakeLists.txt:10` says version 0.1.0, and 0.1.0 is hard-coded in NSIS/`package.ps1`.
- **Impact:** Releases can be built with a version that does not match the tag or the input the operator supplied.
- **Remediation:** Derive the version from the git tag.

### MB-185 — Info, Confirmed — Git: `joinCommand` shell-unsafe reconstruction (display only)

- **Sources:** Part 3 V-62
- **Files:** `app/tools/GitTools.cpp:73-81`
- **Evidence:** `if(part.contains(' ')) part="\"%1\"".arg(part);` — quotes only spaces; `"`, `$`, `` ` ``, `\`, `!` are not escaped. The string is stored in `output.command` and never executed in this file (execution is argv-based, `:135-137`).
- **Impact:** Only if a user/LLM copies the displayed string into a shell.
- **Remediation:** Use proper shell-quoting (single-quote + `'\''` escaping) or drop the reconstructed command from output and show only the argv list.

### MB-186 — Info, Suspected (latent, not exploitable in current wiring) — MCP: prompt injection via untrusted tool descriptions/results

- **Sources:** Part 3 V-73
- **Files:** `app/mcp/MCPClient.cpp:577-587` (`tools/list`: description/`inputSchema` copied verbatim, no sanitising/delimiting), `:469-600` (`processJsonMessage`: tool results, resources, prompts forwarded verbatim via `toolResult`/`resourcesDiscovered`/`promptsDiscovered`)
- **CWE:** CWE-94, CWE-1426
- **Evidence:** No escaping, tagging, or provenance marking of server-supplied text in any of the 8 MCP files. However, a repo-wide search finds **no** consumer feeding MCP tool descriptions/results into a model/system prompt — `ToolCalling` and `MCPClient` are not instantiated outside `app/mcp/`, and `AgentOrchestrator` uses only its own `ToolRegistry`. So this is **not** confirmed exploitable today.
- **Impact (if/when MCP tools are wired into the agent):** A malicious server's tool description ("ignore previous instructions, exfiltrate…") or tool result becomes model input and can steer the agent.
- **Remediation:** When wiring MCP tools in, wrap descriptions/results in clearly delimited untrusted-data blocks, never place them in the system prompt, label the origin per-server, and strip/escape instruction-like language before model ingestion.

### MB-187 — Info, Suspected (latent) — MCP Registry: `installCommand` is untrusted remote data with no consumer yet

- **Sources:** Part 3 V-74
- **Files:** `app/mcp/MCPRegistry.cpp:36-44` (`parseServerListings`: `s.installCommand=p.value("identifier").toString();`), `app/mcp/MCPRegistry.h:19` (`installCommand`), plus unused `requiredEnv` (`:20`)
- **CWE:** CWE-78
- **Evidence:** `installCommand`/`requiredEnv` are parsed from a remote registry response with no validation. A repo-wide search finds **no** code executing or even reading `installCommand` — registry providers are not instantiated anywhere outside `app/mcp/`.
- **Impact:** None today; if a future "Install" button runs `installCommand`, it becomes remote-code execution from the registry.
- **Remediation:** Do not execute identifier strings; if installation is added, resolve packages through a pinned allowlist, verify signatures/hashes, show the exact command for user approval, and never auto-run it.

### MB-188 — Info, Confirmed — `ChatOrchestrator::sendMessage()` is a stub that silently reports success

- **Sources:** Part 4 I1
- **Files:** `app/chat/ChatOrchestrator.cpp:7-11`
- **CWE:** None (correctness / fail-open signalling)
- **Evidence:** `void ChatOrchestrator::sendMessage(const QString &conversationId, const QString &text) { Q_UNUSED(conversationId); Q_UNUSED(text); // Boundary: wired to InferenceManager + MCP tool loop in Phase 2/5. emit generationFinished(conversationId); }`
- **Impact:** None today — no input is used, logged, or sent anywhere, so there is no injection, SSRF, or auth issue in this file. The unconditional `generationFinished` is a placeholder that could mask a future failure if callers assume success. When the Phase 2/5 wiring lands, this is where prompt-injection (MB-110), tool-call, and MCP-server trust issues will need to be audited.
- **Remediation:** No change needed now; when implementing, validate `conversationId`/`text`, surface real errors via `generationError()`, and do not emit `generationFinished` on failure.

### MB-189 — Info, Confirmed — `CodeIndexer`: SHA-1 for chunk IDs / project namespace

- **Sources:** Part 4 I2
- **Files:** `app/storage/CodeIndexer.cpp:30-32, 148-151, 199-200`
- **CWE:** CWE-327
- **Evidence:** `hashString()` uses `QCryptographicHash::Sha1`; `projectNamespace()` is `"code:" + SHA1(normalised root)` and chunk IDs embed `SHA1(rel)`.
- **Impact:** SHA-1 collisions are demonstrably feasible with significant resources; a collision could mix two projects' namespaces or cause `removeByNamespace()` (`:293`) to delete another project's chunks. Not a practical attack here — IDs are not a security boundary.
- **Remediation:** Switch to SHA-256.

---

## 11. Cross-cutting themes

These themes recur across areas. Fixing a theme once, in one place, closes many findings at once — which is why the roadmap in §14 is organised by theme, not by finding count.

### 11.1 No sandbox — the theme that bounds everything else

MB-004, MB-005, MB-006, MB-007, MB-008, MB-038, MB-039, MB-040, MB-070, MB-071, MB-072. The terminal, code runner, MCP servers, git, and the (latent) filesystem tools all run as the app user with full filesystem/network access and the full parent environment. There is no chroot/container/seccomp/AppArmor/`setrlimit`/privilege drop anywhere, and no `--safe-mode`. Until a sandbox exists, every other execution control is a speed bump, not a boundary.

### 11.2 The approval gate is not a control yet

MB-021, MB-022, MB-023, MB-024, MB-025, MB-058, MB-073, MB-079, MB-080, MB-081, MB-082, MB-140, MB-150, MB-151, MB-152, MB-155. The gate defaults to fail-open, trusts the tool's self-reported level, can be disabled with one call, can be bypassed session-wide with no auth, scopes approval by cwd alone, emits raw arguments alongside its redacted preview, has no timeout on pending approvals, and has no live UI consumer on the MCP side. The ChatView approval UI is a stub. Fix the gate *before* wiring more tools into it.

### 11.3 Path containment is lexical, opt-in, and mostly not called

MB-010, MB-020, MB-030, MB-031, MB-052, MB-084, MB-085, MB-086, MB-089, MB-090, MB-091, MB-094, MB-101, MB-102, MB-153, MB-159, MB-174, MB-178. The same flaw appears in TerminalTool, GitTools, FileSystemTools, FileWatcher, CodeIndexer, SecretStorage, the model downloader, the updater, and the engine installer: lexical `cleanPath()`/`absolutePath()` checks that do not resolve symlinks, opt-in or empty sandbox roots, checks done before `mkpath()` creates directories, and check-then-open TOCTOU. `PathSecurity::isInside()` — the one helper that exists — is itself symlink-blind and is called by nobody. The fix is one canonical, symlink-resolving helper, mandatory non-empty roots, and re-validation after open.

### 11.4 The supply chain is unsigned end to end

MB-009, MB-041, MB-042, MB-043, MB-044, MB-045, MB-046, MB-054, MB-055, MB-096, MB-097, MB-115, MB-117, MB-119, MB-120, MB-123, MB-124, MB-125, MB-128. Releases are unsigned, the engine binary is unverified, the update key is a placeholder, the manifest is unsigned, model downloads are unverified, CI actions are unpinned, and the signing pipeline writes a PFX to disk with no cleanup. A single compromised release, GitHub account, or upstream action can ship code to every user with no detection. The updater failing closed (MB-044) is correct behaviour — but it also means no security fix can currently be shipped safely, which makes this theme the gating item for all other fixes.

### 11.5 The server fails open

MB-001, MB-002, MB-003, MB-011, MB-012, MB-013, MB-014, MB-015, MB-016, MB-017, MB-059–MB-069, MB-132–MB-139. Mock mode, unsigned webhooks, unauthenticated billing endpoints, substring tier matching, non-atomic credit grants, check-then-charge settlement, wildcard CORS, `TRUST_PROXY` spoofing, and silently-disabled rate limiting are all instances of the same pattern: a missing or wrong configuration makes the server *more* permissive, never less. Every server control in this document should fail closed.

### 11.6 Secrets are stored, logged, and forwarded carelessly

MB-025, MB-027, MB-037, MB-053, MB-062, MB-095, MB-105, MB-106, MB-107, MB-108, MB-140, MB-146, MB-155, MB-160, MB-177, MB-179. MCP tokens in plaintext JSON, secrets indexed into plaintext SQLite and handed to an embedding provider, credentials echoed in git command output, tokens left in logs by a broken redactor, plaintext passwords in mock mode, a plaintext database with no permissions, and tool arguments shown in plaintext in the UI. No hardcoded secrets were found in source — the problem is handling, not storage in the repo.

### 11.7 Resource exhaustion is unguarded almost everywhere

MB-039, MB-040, MB-074, MB-075, MB-079, MB-087, MB-088, MB-093, MB-096, MB-104, MB-117, MB-135, MB-138, MB-144, MB-161, MB-164, MB-165, MB-169. Unbounded downloads, unbounded search/copy/undo, unbounded MCP input, unbounded tool output, unlimited reconnects, unlimited concurrency, and unbounded in-memory growth. Timeouts exist for one-shot tools (good) but do not bound resource *rate*, and several caps are applied only after the data has already been read into memory.

### 11.8 Prompt injection is a future risk being built today

MB-032, MB-033, MB-034, MB-092, MB-110, MB-186, MB-188. The plan is accepted verbatim from JSON, the planner turns substrings into destructive tools, tool output and memory are replayed as undelimited trusted context, skill delimiters are spoofable, conversation roles are not allowlisted, and MCP tool descriptions/results are forwarded verbatim. None of these is end-to-end exploitable today, because the LLM wiring is not in place. All of them must be fixed *before* that wiring lands, not after.

---

## 12. Safe and mitigating patterns found

These were explicitly checked and found safe. They are recorded so that a future change does not accidentally remove them, and so that this document is not read as "everything is broken".

**Server (from Part 2):**

- **No hardcoded secrets in scope.** `.env.example` contains only placeholders (`your-…-key`, `plan_xxx`, `sk-or-v1-xxx`).
- **No SQL injection in JS.** All DB access is via the Supabase JS client (parameterised `.eq()` / RPC). No string-concatenated SQL in `server/src/`. The SQL RPCs (`grant_credits`, `deduct_credits`, `record_usage`) use `SELECT … FOR UPDATE` and check balances atomically — concurrent deductions cannot overdraw in live mode.
- **No NoSQL injection.** No Mongo/document store; JSONB metadata is passed as a parameter.
- **Webhook Standard-Webhooks verification is correct.** Raw-body parsing before the JSON parser (`app.js`), HMAC-SHA256, `crypto.timingSafeEqual`, 300 s timestamp tolerance, `whsec_` secret decoding, multiple-signature support. The weaknesses are the legacy fallback (MB-065), the unsigned bypass (MB-003), and trust in the payload (MB-013, MB-015, MB-017) — not the HMAC itself.
- **Webhook idempotency exists.** Live inserts into `webhook_events` (unique id, 23505 → duplicate) and deletes the row on handler failure so Whop can retry; mock uses `rememberWebhook`/`forgetWebhook`. Caveat: the key is header-derived (MB-065), not body-hash-derived.
- **Credits are computed server-side.** `/ai/chat` derives cost/credits from the catalogue (`costUsdFor`/`creditsForUsage`), never from client-supplied credits/cost fields.
- **Model allowlist is enforced.** `resolveModel` rejects anything not in `MODELS`; a startup assertion throws if a forbidden model (GPT/Claude/Gemini/OpenAI/Anthropic/Google) is in the catalogue. `PASS_THROUGH_PARAMS` does **not** include `model`.
- **Mock tokens are random.** `crypto.randomBytes(32)` base64url, not derived from user data.
- **`/billing/mock-success` escapes output.** `escapeHtml(tier)` covers `&<>"'` — the reflected tier query param is not XSS as written.
- **RLS in `schema.sql` is well-structured.** RLS is enabled on all user tables; authenticated users SELECT only their own rows and UPDATE only `display_name`/`updated_at` on profiles; credit RPCs are revoked from anon/authenticated and granted only to `service_role`; `SECURITY DEFINER` functions set `search_path=public`; `webhook_events` has no client policies and is revoked from anon/authenticated; `credit_summary` is revoked from clients. Caveat: this is the schema *file*, not proof the live Supabase project has it applied.
- **Body limits exist.** `express.json({limit:'2mb'})`, webhook raw body 1 MB.
- **No cookies, so no CSRF token issue.** Auth is `Authorization: Bearer` only, with no session cookies.
- **No SSRF in server code.** `whopRequest` has a fixed base URL + constant path; `openrouter` has a fixed base URL. The only SSRF-adjacent surface is the user-supplied `image_url` forwarded to OpenRouter (MB-067).
- **No secret logging found.** `console.error` logs error objects for 5xx (see MB-059), but no code logs `Authorization` headers, tokens, or env values.

**Agent / tools / MCP / skills (from Part 3):**

- **No shell in git/MCP/FileSystemTools.** `QProcess::start("git", …)` with a `QStringList` (`GitTools.cpp:135-137`; `GitTools.h:7-9` comment is accurate); `MCPClient::startStdio` uses `QProcess::start(command,args)` (`MCPClient.cpp:165`) — no `/bin/sh`, `cmd.exe`, or `system()` in `app/mcp/` or `app/tools/GitTools.*`. The commit message (`GitTools.cpp:285`) and diff/log `--opt=value` (`:221,411-415`) are single-argv values. The risk is the arguments/commands themselves, not shell parsing.
- **Non-shell argv separation.** `TerminalTool.cpp:153-155,295` and `CodeRunner.cpp:143-148,205,254` — shell metacharacters in arguments are not interpreted when `shell=false`. `CodeRunner.h:6-10` header claim is accurate.
- **Output caps (one-shot tools).** `TerminalTool.h:60` / `TerminalTool.cpp:246-252` and `CodeRunner.h:82` / `CodeRunner.cpp:257-268` — 1 MiB per stream, with a truncated flag. Git: 1 MiB per stream (`GitTools.cpp:157-159`). ToolCalling: 1 MiB post-parse (`ToolCalling.cpp:201-210`) — but see MB-075 (post-parse only).
- **Timeouts (one-shot tools).** TerminalTool (`:196`, 100 ms–600 s), CodeRunner (`:79`, 1 s–600 s), git 30 s default (`GitTools.cpp:130,143-151`, `kill()` on timeout), MCP per-request 30 s (`MCPClient.cpp:712-740`, `MCPClient.h:230`).
- **Language allowlist.** `CodeRunner.cpp:13-29,110-115` — only cpp/python/javascript (after normalisation); anything else errors. Fixed compiler flags: `-std=c++17 -O0 -pipe <source> -o <binary>`; no model-controlled flags.
- **Lexical `..` traversal is blocked** (symlink bypass remains, MB-020): TerminalTool (`:50-61,167`), GitTools `pathArgument` (`:55-69`, rejects `../` and NUL; diff/log use `--` before the path, `:245,429`), FileSystemTools `resolvePath` NUL/empty rejection (`:71-79`), canonicalisation for existing paths + boundary-aware `isInsidePath` (`:37-53,70-96`).
- **Bounds that work.** Git log count `qBound(1,…,500)` (`GitTools.cpp:407-408`); `read_file` 1 MB cap with bounded offset/limit (`FileSystemTools.cpp:142-157`); `list_dir` `maxEntries` 1–10000 with a truncated flag, hidden files off by default (`:446-451`); `search_code` 2 MB/file + binary skip (`:532,565`); `edit_file` rejects an empty `oldText` and an invalid regex; undo refuses when the file changed since the edit (`FileEditHistory.cpp:47-53`); watcher debounce bounded (`FileWatcher.cpp:197-201`, `qBound(0,ms,5000)`).
- **Post-approval argument TOCTOU (registry layer).** `execute()` copies args into `PendingCall` (`ToolRegistry.cpp:185-188,208`) and `beginExecution()` executes that snapshot (`:228-247`), so mutating the caller's `QJsonObject` after approval cannot change what runs. (TOCTOU *inside* a tool — for example a path checked then the file replaced — is MB-085.)
- **Stale/double decisions at the registry layer.** `onApprovalDecided` / `onApprovalCancelled` / `onWatchdog` all re-check `m_running` and `awaitingApproval` (`ToolRegistry.cpp:290-328`); `finishCall` returns early if the call is already gone (`:250-262`).
- **Default approval mode.** `ToolApproval` defaults to Prompt (`ToolApproval.h:111`); AutoDeny exists — unsafe modes require explicit opt-in. Grants are in-memory only in the audited files (no persistence code in scope, so they do not survive a restart — verify no other layer persists them).
- **Validation that does exist.** `validateArgs()` enforces required, per-property type, enum, and recursive validation of nested objects (`ToolRegistry.cpp:118-180`); `registerTool()` rejects null/empty-name/duplicate tools (`:41-52`); `unregisterTool()` fails in-flight calls rather than leaving them hanging (`:53-72`).
- **No clone/fetch/config tool in GitTools.** `grep clone|fetch|config|upload-pack|ext::` returns nothing (except `checkout` in a comment) — clone SSRF, `--upload-pack`/`--config` injection, and git config injection are **not** present in this scope; the risk enters instead via push/pull remote URLs (MB-008).
- **SQL injection — safe.** All AgentMemory SQLite access uses `prepare()` + bound `?` values (`AgentMemory.cpp:257-380`). No string-built SQL.
- **Skill-id traversal on install — safe.** `SkillsManager.cpp:138-144` rejects `/`, `\`, `..` in an id; the frontmatter id is explicitly ignored as canonical (`~173-177`). Skills are disabled by default (`SkillsManager.cpp:158`, `s.enabled=false`; `SkillInjector.cpp:105-111` only injects enabled skills).
- **Loop bounds — mostly present.** `AgentPlanner.h:68` `m_maxSteps=12`, `appendStep()` caps at it (`AgentPlanner.cpp:99-102`); `AgentOrchestrator.h:100` `m_maxToolSteps=20`, checked in `advance()` (`AgentOrchestrator.cpp:88-93`); `refine()` appends are also capped by `appendStep()` (`:205-231`). No unbounded planner/orchestrator recursion — `advance()` is queued and terminates when no step is Pending.
- **Observation/context caps — present.** `emitObservation` 4000 chars (`AgentOrchestrator.cpp:197-202`), memory note 240 (`:151-153`), run-log observation 2000 (`:245`), `buildContextSummary` note 300 (`AgentMemory.cpp:182`), plan preamble goal 400 / context 1200 (`AgentPlanner.cpp:131-133`), skill token budget 4000 with a truncation marker (`SkillInjector.cpp:76-89`, `SkillInjector.h:42`).
- **Skill content labelled untrusted — partial.** `SkillInjector.cpp:23-36` wraps skills in `<<<BEGIN/END UNTRUSTED SKILL…>>>` delimiters (see MB-092 for the spoofing limits).
- **SkillsShClient error handling — partial.** HTTP ≥400 and 401/403 are surfaced (`SkillsShClient.cpp:84-96,111-117,138-146`); JSON parsing guards `doc.isNull()`/`isObject()` (`:44-77,120-122,152-157`).
- **TLS verification is not disabled in MCP.** No `setPeerVerifyMode`, `ignoreSslErrors`, or `sslErrors` handling in `app/mcp/`; Qt default verification applies. (No pinning; `http://` is still allowed — MB-026.)
- **JSON-RPC response matching.** `processJsonMessage` (`MCPClient.cpp:469-518`) matches responses by numeric id against `m_pending`, logs unexpected ids, and rejects non-object/non-numeric-id messages. The third-party registry is off by default (`MCPRegistry.cpp:138,164`; the header states no unverified third-party API is contacted by default). Server→client requests are mostly inert: only `ping` is auto-answered (`MCPClient.cpp:497-500`).
- **No unsafe deserialisation in scope.** Only `QJsonObject` args; no pickle/eval/manual parsing in FileSystemTools/GitTools; no code deserialisation/eval in the agent/skills scope; no credentials logged in the agent/skills scope.
- **No delete/rename tool in FileSystemTools scope.** The only deletion is `FileEditHistory::undoLast()` removing a file the agent itself created (`:57`), and it removes the symlink itself, not the target. No `QDesktopServices`, `openUrl`, `startDetached`, `QProcess`, or `system()` in the FileSystemTools/FileWatcher/SystemIntegration files. `git_status` (`GitTools.cpp:189-194`) takes only booleans — safe.
- **Temp-dir cleanup and single-job serialisation.** `CodeRunner.cpp:73,111,128,315-330` (`QTemporaryDir` auto-removal, `abortJob()` on a new run, destructor calls `abortJob()` `:40`); `TerminalTool.cpp:76-79` destructor cancels running calls.

**Core / storage / models / inference / hardware / chat (from Part 4):**

- **SQL injection: not present.** Every parameterised query in `Repositories.cpp:13,22` and `VectorStore.cpp` (for example `:102-114,143,163,183,207-216,332-336`) uses `?` bind values; `Database.cpp` uses only static literals. `likePattern()` (`VectorStore.cpp:20-26`) correctly escapes `\`, `%`, `_` and the `LIKE` queries use `ESCAPE '\'` (`:365-378`). The only injection-adjacent issue is FTS5 query *syntax* (MB-103), not SQL.
- **Command injection (in scope): not present in `HardwareDetector::runProcess()`** — it uses `QProcess::start(program, arguments)` with no shell and hardcoded literal arguments (`HardwareDetector.cpp:127-130,227-229,290,309-319`). The command-injection findings are elsewhere: the PowerShell string building (MB-043) and `PATH`/CWD resolution (MB-041, MB-113). `Updater.cpp:348-361` uses `QProcess::startDetached(path, {})` / `msiexec` with an argument list (not a shell string), so it is not command-injectable via that path.
- **Buffer overflows / unsafe C functions: not present.** No `strcpy`/`strcat`/`sprintf`/`gets`/`system`/`popen`/`eval`/`memcpy` on untrusted lengths in any of the 53 files. `LlamaBackend` checks `llama_token_to_piece` for `< 0` before `QString::fromUtf8(buf, n)` (`char buf[256]`), `ChatTemplate` checks `written > 0`, the manual `quint32` assembly in `HardwareDetector.cpp:190-197` is guarded by `bytes.size() >= 4`, `ggml_backend_reg_name`/`dev_name`/`dev_description` results are null-checked before `QString::fromUtf8()` (`BackendDetector.cpp:186-187,200-203`), and `ChatTemplate`'s `roles`/`contents` are `reserve()`d before filling so the `llama_chat_message` `constData()` pointers stay valid during `llama_chat_apply_template` (`ChatTemplate.cpp:48-66`).
- **Secrets in source / hardcoded credentials: none found** — no hardcoded passwords, API keys, or tokens in any of the 53 files (the only "key" is the updater's *placeholder* public key, MB-044, which is invalid by design). `WhopClient` holds no secrets (`WhopClient.h:14` comment is honoured).
- **Secrets in logs/files (in scope):** No token/path/credential logging was found in models/inference (only a generic `qWarning() << "llama.cpp not available"`); the HF token is session-only in memory with no disk persistence found in-repo. The logging risk is the redactor itself (MB-108), not a secret being logged deliberately.
- **HTTP (cleartext) in models/inference: none** — all in-scope URLs there are `https://` (`huggingface.co`, `api.github.com`). No `IgnoreSslErrors`/custom TLS verification. The cleartext issues are in core/billing (MB-047, MB-098).
- **Shell injection (tar/unzip): safe as written** — `QProcess::start("tar", {…})` / `unzip` use argv, not a shell. Only the PowerShell path (MB-043) builds a command string. Note: `archivePath` is `installRoot()`-prefixed, so it cannot start with `-` (no option injection).
- **Local API auth bypass: not applicable** — `LocalApiServer` binds nothing (MB-157). **Model-file deserialisation (Python `pickle` etc.): not present** — models are GGUF parsed by llama.cpp; the risk is native-parser safety (MB-051), not deserialisation of executable objects.
- **Updater HTTPS enforcement (as written).** `Updater` enforces HTTPS on the manifest, download, and redirects (`Updater.cpp:37-48, 95, 123-129, 156, 187, 227`), verifies SHA-256 + signature and fails closed on a missing signature (`:255-284`); `Logging` does attempt redaction (but see MB-108); `Theme`/`Navigation` are hardcoded colours / UI state with no file, network, or exec surface; `ModelDownloader` sets `NoLessSafeRedirectPolicy` (`ModelDownloader.cpp:36`) — `EngineInstaller`/`HuggingFaceClient` lack it (MB-115, MB-099); `HuggingFaceClient::searchModels` correctly uses `QUrlQuery` (`:24-32`); `CodeIndexer`'s exclude-dir logic re-checks every file's path components against excluded dirs (`:242-253, 302-311`), so the acknowledged non-pruning of `QDirIterator` (`:229-230` comment) does not by itself leak excluded-dir files — the gap is symlinks/secrets (MB-053, MB-102), not pruning; `VectorStore` binary handling is safe — `blobToVector()` rejects blobs whose size is not a multiple of `sizeof(float)` (`:524`), `cosineSimilarity()` checks empty/size-mismatch/zero-norm (`:532-547`), `hybridSearch()` clamps `semanticWeight` to `[0,1]` (`:404`), `chunkFile()` clamps `chunkLines`/`overlap`/`step` (`:172-174`); `SecretStorage`'s primitive is sound — DPAPI (`CryptProtectData`/`CryptUnprotectData`, `:23/:49`) with default user scope is the correct Windows primitive and the blob on disk is encrypted, not plaintext (the issues are traversal, entropy, zeroing, and the non-Windows stub — MB-052, MB-106, MB-107).

**Build / CI / QML / docs (from Part 1):**

- **Real C++ tests that do cover security.** `tests/tools/test_builtin_tools.cpp:121-133, 196-220` — path-traversal escape rejected, bad cwd rejected, terminal timeout. `tests/tools/test_tool_registry.cpp:126-140, 150-215, 235-238` — arg validation, unknown-tool rejection, approval gate, secret redaction (`api_token` masked). `tests/agent/test_agent.cpp:183-227` — orchestrator approval flow and cancellation. `tests/mcp/test_mcp_client.cpp`, `test_tool_calling.cpp`, `test_agent_tool_loop.cpp` — MCP JSON-RPC, arg validation vs server schema, timeouts, malformed-line handling via `tests/mcp/mock_server.py`. `tests/integration/test_credits_flow.js:1-113` — real end-to-end credits deduction vs a spawned server on `127.0.0.1:3099`. It uses a hard-coded test password and inherits `process.env` — never run it against production.
- **Safe in the build/CI/QML scope.** No `pull_request_target`, no `${{ github.event… }}` interpolation in `run:` steps, no `secrets.*` echoed to logs. No WebView/WebEngineView, no `eval()`, no `runJavaScript`, no `Qt.openUrlExternally` in QML. The only RichText is `TerminalView.qml:291`, and input is escaped in `escChar()` (`&`→`&amp;`, `<`→`&lt;`, `>`→`&gt;`). No hard-coded secrets/tokens/private keys were found by grep in scope. `server/.env.example` contains only placeholders. Signing templates contain only placeholder paths/env names. `.gitmodules` uses `https://github.com/ggml-org/llama.cpp.git`. The LICENSE is standard GPLv3 (674 lines) — note that distributing binaries under GPLv3 requires offering corresponding source. QML password fields are correctly masked: `LoginView.qml:114`, `SettingsView.qml:44` (`echoMode: TextInput.Password`).

---

## 13. Test gaps

- **No C++ test** for Updater signature verification, SecretStorage (C++), LocalApiServer auth/CORS, model-download validation (MB-130), or MCP-server-add validation (MB-131).
- `tests/security/*.js` are stubs that test nothing (MB-056).
- `tests/inference/test_inference.cpp:106,109,155,158` — model-backed tests `QSKIP` unless `MODELFIT_TEST_MODEL` is set and llama.cpp is built; inference is largely untested in CI.
- `tests/unit/test_mcp_jsonrpc.cpp`, `test_hardware.cpp`, `test_skills_parser.cpp` — trivial smoke tests only.
- `tests/mcp/test_mcp_client.cpp:246`: `QVERIFY(responded == false || true);` — a tautology, not a test.
- The release test step runs the wrong preset and masks failures (MB-121); `build.yml` has no test step at all.
- Prior audit status (from Part 1): BUG-202, 203, 204/205, 209 appear fixed in HEAD `be192b4`. BUG-207 (SecretStorage path traversal) is MB-052 in this document and is confirmed present; `todo.md:67` still lists it as open.

**Tests that should be added first** (each maps to a finding that must be proved fixed, not just claimed fixed): updater rejects a bad/missing signature (MB-044); model download rejects a traversal destination and a cancelled download is not `finished` (MB-010, MB-048, MB-049); engine install rejects a bad hash (MB-009); an unsigned webhook is rejected in production mode (MB-003); checkout/entitlements require auth (MB-011, MB-012); the approval gate fails closed when a tool forgets its level and when `setApproval(nullptr)` is called (MB-021, MB-022); a symlink inside the sandbox root does not escape it (MB-020, MB-031, MB-101); a `servers.json` with a changed command requires re-confirmation (MB-007).

---

## 14. Prioritised remediation roadmap

Order matters: earlier items unblock or bound later ones. Do not start at the bottom.

1. **Fail the server closed (MB-001, MB-002, MB-003).** Refuse to start in production without Supabase/Whop/OpenRouter configured; delete the mock-grant and unsigned-webhook paths from any non-local deployment. This is the only item that can be exploited remotely today with no authentication.
2. **Sandbox first (MB-005, MB-004, MB-006, MB-038).** No agent tool should run unsandboxed as the app user. Only this fix bounds all the execution findings.
3. **Fix the approval gate (MB-021–MB-025, MB-073, MB-080, MB-081, MB-082).** Fail-closed defaults, reject `setApproval(nullptr)`, remove `Q_INVOKABLE` bypass mutators, stop emitting raw args, enforce the output cap, add approval TTLs/budgets.
4. **Fix path containment once, everywhere (MB-101, MB-020, MB-030, MB-031, MB-084–MB-086, MB-094, MB-102).** One canonical, symlink-resolving helper, mandatory non-empty roots, `O_NOFOLLOW` + re-validation, no direct-write fallback.
5. **Close the git and MCP remote-execution paths (MB-008, MB-028, MB-029, MB-007, MB-026, MB-027, MB-037, MB-019).** Remote allowlists, refspec construction server-side, signed/protected `servers.json`, HTTPS-only + private-IP blocking, tokens in the OS keyring, minimal environment for all children.
6. **Fix billing enforcement (MB-011, MB-012, MB-015, MB-013, MB-014, MB-017, MB-016, MB-064, MB-065, MB-069).** Auth on every billing route, exact-match plan mapping only, Whop verification + `current_period_end`/status enforcement on every gated request, reserve credits before the upstream call, atomic grants.
7. **Fix the supply chain so fixes can be shipped (MB-044, MB-045, MB-046, MB-096, MB-097, MB-054, MB-055, MB-009, MB-042, MB-043, MB-115, MB-117).** Real signing key + correct Ed25519 verification (re-verified at install time), unpredictable/symlink-safe temp files, a signed manifest, mandatory release signing + checksums/SBOM, hash/signature verification of the engine binary, a validating extractor, no PowerShell command strings, allowlisted download hosts.
8. **Fix model downloads (MB-010, MB-048, MB-049, MB-100, MB-130).** Destination containment + host allowlist in `download()`, SHA-256/size verification of completed downloads, fix cancel-as-success, `.part` + atomic rename.
9. **Fix hardware detection (MB-041, MB-111, MB-112, MB-113).** Stop loading libraries/helpers from CWD/`PATH`; canonical, allowlisted, verified paths only.
10. **Fix account/billing client (MB-047, MB-098, MB-109, MB-063).** HTTPS-only, authenticated traffic; fix false-success/logout-revocation; enforce entitlements server-side.
11. **Fix inference (MB-050, MB-051, MB-110, MB-114, MB-116).** Implement the documented worker thread / serialise all engine access; validate model files and inference config; role allowlist before LLM wiring; cap template output.
12. **Stop the agent executing unverified plans/skills (MB-032, MB-033, MB-034, MB-035, MB-036, MB-091, MB-092).** Plan allowlist + whole-plan approval, delimited/untrusted memory and skill content, block-on-error skill scan, allowlisted skills.sh base URL.
13. **Fix storage/secrets (MB-052, MB-053, MB-102, MB-105, MB-106, MB-107, MB-108, MB-099).** Sanitise `SecretStorage` paths, add entropy/zeroing/atomic writes, implement non-Windows keychain storage; symlink/secret exclusions in the indexer; encrypt the DB; fix log redaction.
14. **Storage robustness (MB-103, MB-104, MB-162, MB-163, MB-165, MB-166, MB-167).** FTS escaping, `topK` clamping, transactional FTS writes, checked migrations, DB thread-safety, setting allowlists.
15. **Resource limits (MB-039, MB-040, MB-074, MB-075, MB-087, MB-088, MB-089, MB-093, MB-144).** Process groups + rlimits, bounded reconnects, pre-parse size caps, bounded search/copy/undo.
16. **Defence-in-depth (MB-059–MB-062, MB-066–MB-068, MB-132–MB-139, MB-157–MB-183, MB-184–MB-189).** Security headers, validated env vars, auth design for the local API before it is wired up, timeouts/HTTP-status checks, caps, exit-code/overflow checks, `libraryPath` redaction, thread-safety, log rotation/levels, SHA-256 for indexer ids, and the disclosure policy (MB-181).

---

## 15. Responsible disclosure

**There is no vulnerability-disclosure policy in this repository** (MB-181). `docs/SECURITY.md` lists known issues but gives no contact, reporting channel, timeline, scope, or safe harbour, and there is no root `.github/SECURITY.md` or `security.txt`.

Until a policy exists, the minimum is:

- Add a security contact (an email or a GitHub Security Advisory channel) and a scope statement to a root `.github/SECURITY.md`.
- State a response timeline (acknowledgement and fix targets) and a safe-harbour commitment for good-faith research.
- Do **not** treat this file as a disclosure to anyone outside the project: it was written for the repository owner (`kRxZykRxZy`) at their request, and it contains enough detail to exploit several findings. Do not publish it, share it publicly, or attach it to a public issue until the Critical and High findings are fixed or a coordinated disclosure plan is in place.
- When fixes land, remove the corresponding items from `docs/SECURITY.md` only when a real test proves them fixed (MB-057), and consider a changelog entry that credits the audit without publishing exploit detail for any finding that is still unfixed.

---

## 16. Methodology, automated scans, and limitations

### 16.1 Method

Four parallel reviewers, each reading their area in full, followed by a synthesis pass that deduplicated cross-cutting findings:

- **Part 1** — Build / CI / scripts / QML / docs / tests (28 findings, F-01–F-28), completed 2026-11-26T07:46:22Z.
- **Part 2** — Server (`server/` only, 25 files) (29 findings), completed 2026-11-26T07:47:14Z. Line numbers approximate ±~10 lines.
- **Part 3** — Agent core / tools / MCP / skills (41 files, ~9,000 lines, V-01–V-75), completed 2026-11-26T07:48:54Z, itself using 6 parallel reviewers.
- **Part 4** — Core / storage / models / inference / hardware / chat (53 files, 4,615 lines, 4 parallel reviewers), completed 2026-11-26T07:53:34Z.

All four parts were static, read-only audits. No files were modified, nothing was pushed, no code was executed by the reviewers, no exploits were run, and the app was not run.

### 16.2 Automated scans (run during synthesis, 26 November 2026)

- **Repository size:** 26,812 lines in total across 61 `.cpp`, 46 `.h`, 25 `.js`, 19 `.qml`, 17 `.md` files (plus 6 `.txt`, 4 `.json`, 3 `.sh`, 3 `.ps1`, 2 `.yml`, 2 `.mjs`, 1 `.yaml`, 1 `.sql`, 1 `.py`, 1 `.nsi`, 1 `.gitmodules`, 1 `.gitignore`, 1 `.example`, 1 `LICENSE`).
- **Secrets grep** (high-confidence patterns: `sk-…`, `ghp_…`, `AKIA…`, `-----BEGIN … PRIVATE KEY-----`, `xox[baprs]-…`, `hf_…`): **no high-confidence secrets found**.
- **Unsafe C functions grep** (`strcpy`, `strcat`, `sprintf`, `gets`, `system`, `popen`, `eval`): only the string `"eval()"` in `app/skills/SkillsManager.cpp:78`, which is an entry in the skill-scan pattern list — **no actual unsafe call found**.
- **`http://` URLs in `app/` and `server/src/`** (excluding comments/docs): `app/core/AccountManager.h:40` (`http://localhost:3000` default), `app/api/LocalApiServer.cpp:12` (`http://%1:%2/v1` stub endpoint), `server/src/routes/billing.js:29` (`SITE_URL` fallback `http://localhost:…`), `server/src/lib/openrouter.js:46` (`HTTP-Referer` fallback `http://localhost:3000`), `server/src/index.js:9` (log line), `server/src/app.js:80` (CORS defaults `http://localhost:3000`, `http://127.0.0.1:3000`). No other cleartext URLs were found in code.
- **Tracked secret-named files:** `git ls-files` matching `*.env` / `*.pem` / `*.pfx` / `*.key` / `secret` returned only `app/storage/SecretStorage.cpp`, `app/storage/SecretStorage.h`, and `tests/security/test_secret_storage.js` — no tracked secret files.
- **Server dependencies** (`server/package.json`): `@supabase/supabase-js ^2.45.0`, `cors ^2.8.5`, `dotenv ^16.4.0`, `express ^4.21.0`. Lockfile pins (`server/pnpm-lock.yaml`, from Part 2) differ slightly: express 4.22.3, cors 2.8.6, dotenv 16.6.1, `@supabase/supabase-js` 2.119.2, plus body-parser 1.20.9, qs 6.16.2, send 0.19.2, serve-static 1.16.3. There is no helmet, `express-rate-limit`, zod/validator, or password-hashing library in `package.json`.

### 16.3 What was NOT verified

- **Dependency CVEs.** No `npm`/`pnpm audit` was run. Lockfile versions are not obviously ancient, but **nothing in this document can state the dependencies are CVE-free**. Run `pnpm audit` and enable Dependabot/Renovate.
- **Live behaviour / exploitability.** Static only — no requests were sent, the server was not started, no webhook was forged, no race was executed. Items marked Suspected need dynamic confirmation.
- **llama.cpp / ggml parser and backend-loading safety.** `llama_model_load_from_file()` (MB-051), `llama_chat_apply_template()`/Jinja (MB-114), and `ggml_backend_load_all()` (`BackendDetector.cpp:178-179`, `std::call_once`) run vendored native code whose search order, signature checks, parser memory safety, and Jinja sandbox depend on the bundled llama.cpp/minja version — not audited (vendored code, and the submodule was empty in the clone).
- **Server-side enforcement in production.** Whether `schema.sql` is actually applied in the live Supabase project, and Supabase dashboard settings (email-confirmation, MFA, Auth rate-limit/CAPTCHA, JWT expiry, service-role key rotation). Production env values (`ALLOW_UNSIGNED_WEBHOOKS`, `RATE_LIMIT_DISABLED`, `MOCK_STARTING_CREDITS`, `TRUST_PROXY`, `SITE_URL`/`ALLOWED_ORIGINS=*`); whether a real `.env` was committed; whether the server sits behind the Nginx proxy in DEPLOY.md (which itself has no security headers, no proxy rate limiting, and no `X-Forwarded-For` stripping note). Whop dashboard state (actual plan IDs/prices/names, webhook secret rotation, Standard Webhooks vs legacy HMAC configured scheme). OpenRouter actual billed prices vs the 2026-11-23 snapshot in `models.js`, and real prompt-token counts vs the `len/4` estimate.
- **Runtime `PATH` / CWD / install-directory writability** — whether the app is actually launched from a user-writable CWD, and whether the install dir is user-writable — determines the exploitability of MB-041/MB-111/MB-113. Likewise whether symlinked files are actually returned by `QDirIterator` in this Qt version, DB file permissions as created on disk, whether `Database::openAndMigrate()` is ever called twice with the fixed `"modelfit"` connection, and `QUrl::fileName()` decoding of percent-encoded separators (MB-159).
- **Qt TLS / redirect behaviour** — default certificate verification, whether `Authorization` is stripped on cross-host redirects (MB-099, MB-026), and whether `HAVE_OPENSSL` is defined in the shipped build (`CMakeLists.txt:153-157` makes it conditional on `find_package(OpenSSL QUIET)`) were not runtime-tested; no `ignoreSslErrors` was found in scope.
- **Callers.** Who calls `createSecretStorage()`, `setSetting()` (especially whether API keys/tokens go through it), `CodeIndexer::indexProject()`/`chunkFile()`, and from which threads; whether any caller falls back to plaintext on Linux/macOS (MB-107) or stores the HF/access token elsewhere; whether `hardwareInfo()` JSON (MB-173) or the log file ever leaves the device (telemetry/crash reports); and whether the embedding provider is local or a remote API (MB-053). This limits several findings to Confirmed-code / Suspected-impact, as marked.
- **QML trust boundary.** All `Q_INVOKABLE` methods are callable from any QML in the app; whether untrusted QML/content can ever be loaded was not in scope. No QML consumer was found for `WhopClient::checkoutSessionReady`, `Updater::setManifestUrl`/`checkForUpdates`/`downloadUpdate`/`installUpdate`, or `LocalApiServer::start()` — their downstream impact (for example opening a checkout URL) could not be confirmed in-repo.
- **Future LLM wiring.** `ChatOrchestrator` is a stub and conversation messages are not fed to any model in the audited code; MB-110's prompt-injection impact can only be confirmed once the Phase 2/5 `InferenceManager`/MCP integration exists. Cross-thread callers of `ConversationManager` (MB-170) and of the storage layer (MB-166) were likewise not found.
- **Zip-slip exploitability on the target platform** (MB-042): GNU `tar`/`unzip`/`Expand-Archive` have differing default sanitisation; whether a crafted archive actually escapes needs a runtime extraction test.
- **Production wiring / end-to-end enforcement (agent scope).** Whether `ToolRegistry`/`ToolApproval` actually gates a malicious remote/branch/plan in production; whether `AutoApprove` is ever set in production; whether `ToolApproval` is reachable from QML via `agent.approval()`; whether `alwaysAllowed`/scope rules or approval mode are persisted/restored by another layer; whether the approval log is ever written to disk; whether `TerminalTool` is constructed in production with a non-empty sandbox root, and whether `CodeRunner::run()` / `TerminalSession::start()` are reachable by the model vs only by the user (for example via QML).
- **Agent loop / prompt injection end-to-end.** The `AgentOrchestrator` step budget, how model output becomes `step.toolName`/`step.args`, whether tool output is re-injected as trusted instructions; any LLM prompt construction / chat inference consumer of memory or skill blocks; any QML consumer of `agent.run()`, `planReady`, `approvalRequired`, or `exportRun()`.
- **Runtime / dynamic behaviour (no execution).** Whether `ext::` actually executes with the installed git version/protocol defaults; whether git follows symlinked pathspecs; exact git push refspec/option-parsing edge cases; symlink-cwd bypass, orphan persistence after kill, `LD_PRELOAD`/`PATH` hijack, stdin-blocking, `QSaveFile` fallback, `hashOf()` on FIFOs/`/dev/zero`, symlink handling in `copyTree` — all code-reasoned only, no PoCs run.
- **Runtime environment.** What secrets (if any) live in the parent process env, whether the app runs as root/a privileged user, PATH writability, FS permissions, network egress policy, git binary/version, PATH, and parent-process environment; whether `servers.json` is actually `0644` at runtime (depends on umask) or whether AppData is shared/synced; the Qt version / `QNetworkAccessManager` default redirect policy for the MCP HTTP transport.
- **Windows branches** (cmd `/c`, ConPTY/`CreateProcessW`, UNC paths, case-insensitivity/drive-letter edge cases) were read, not compiled/run. No test suite for the scoped agent files was examined. `openSseStream()` (`MCPClient.cpp:207-237`) is dead code — no caller in the repo.
- **Git history.** The clone has a single squashed commit (`be192b4`), so prior history could not be audited for leaked secrets or earlier vulnerabilities. The `third_party/llama.cpp/` submodule was empty in the clone.

---

## 17. Files reviewed

**Server (25 files, Part 2):** `server/.env.example`, `server/DEPLOY.md`, `server/README.md`, `server/package.json`, `server/pnpm-lock.yaml`, `server/scripts/smoke.mjs`, `server/src/app.js`, `server/src/index.js`, `server/src/env.js`, `server/src/config/models.js`, `server/src/config/pricing.js`, `server/src/lib/auth.js`, `server/src/lib/credits.js`, `server/src/lib/entitlements.js`, `server/src/lib/errors.js`, `server/src/lib/mock.js`, `server/src/lib/openrouter.js`, `server/src/lib/rateLimit.js`, `server/src/lib/schema.sql`, `server/src/lib/supabase.js`, `server/src/lib/whop.js`, `server/src/routes/ai.js`, `server/src/routes/auth.js`, `server/src/routes/billing.js`, `server/src/routes/user.js`.

**Agent / tools / MCP / skills (41 files, ~9,000 lines, Part 3):** `app/main.cpp`; `app/Application.cpp/.h`; `app/tools/CodeRunner.cpp/.h`, `FileEditHistory.cpp/.h`, `FileSystemTools.cpp/.h`, `FileWatcher.cpp/.h`, `GitTools.cpp/.h`, `SystemIntegration.cpp/.h`, `TerminalTool.cpp/.h`, `ToolApproval.cpp/.h`, `ToolRegistry.cpp/.h`; `app/mcp/MCPClient.cpp/.h`, `MCPRegistry.cpp/.h`, `MCPServerManager.cpp/.h`, `ToolCalling.cpp/.h`; `app/agent/AgentMemory.cpp/.h`, `AgentOrchestrator.cpp/.h`, `AgentPlanner.cpp/.h`; `app/skills/SkillInjector.cpp/.h`, `SkillsManager.cpp/.h`, `SkillsShClient.cpp/.h`. `app/core/PathSecurity.cpp/.h` was read as reference only.

**Core / storage / models / inference / hardware / chat (53 files, 4,615 lines, Part 4):** `app/core/` (AccountManager, Logging, Navigation, PathSecurity, Paths, Theme, Updater) + `app/api/LocalApiServer.*` + `app/billing/` (SubscriptionManager, WhopClient) — 20 files, 1,148 lines; `app/storage/` (CodeIndexer, Database, Repositories, SecretStorage, VectorStore) — 10 files, 1,330 lines; `app/models/` (HuggingFaceClient, ModelDownloader, ModelDownloads, ModelRegistry) + `app/inference/` (ChatTemplate, ContextManager, EngineInstaller, LlamaBackend) — 15 files, 1,206 lines; `app/hardware/` (BackendDetector, HardwareDetector) + `app/chat/` (ChatOrchestrator, ConversationManager) — 8 files, 931 lines.

**Build / CI / scripts / QML / docs / tests (Part 1):** `CMakeLists.txt`, `CMakePresets.json`, `tests/**/CMakeLists.txt`, `.github/workflows/build.yml` + `release.yml`, `scripts/`, `.gitignore`, `.gitmodules`, `LICENSE`, `README.md`, `todo.md`, `verify.mjs`, `app/ui/**/*.qml`, `docs/*.md`, `tests/`.

---

## 18. Appendix A — Source-ID to MB-ID mapping

Format: `source ID → MB-ID`. Merged findings list every source ID that fed into the MB finding.

**Server (Part 2):** C1 → MB-001 · C2 → MB-002 · C3 → MB-003 · H1 → MB-011 · H2 → MB-012 · H3 → MB-013 · H4 → MB-014 · H5 → MB-015 · H6 → MB-016 · H7 → MB-017 · M1 → MB-059 · M2 → MB-060 · M3 → MB-061 · M4 → MB-062 · M5 → MB-063 · M6 → MB-064 · M7 → MB-065 · M8 → MB-066 · M9 → MB-067 · M10 → MB-068 · M11 → MB-069 · L1 → MB-132 · L2 → MB-133 · L3 → MB-134 · L4 → MB-135 · L5 → MB-136 · L6 → MB-137 · L7 → MB-138 · L8 → MB-139

**Agent / tools / MCP / skills (Part 3):** V-01 → MB-004 · V-02 → MB-005 · V-03 → MB-006 · V-04 → MB-007 · V-05 → MB-008 · V-06 → MB-018 · V-07 → MB-019 · V-08 → MB-020 · V-09 → MB-021 · V-10 → MB-022 · V-11 → MB-023 · V-12 → MB-024 · V-13 → MB-025 · V-14 → MB-026 · V-15 → MB-027 · V-16 → MB-028 · V-17 → MB-029 · V-18 → MB-030 · V-19 → MB-031 · V-20 → MB-032 · V-21 → MB-033 · V-22 → MB-034 · V-23 → MB-035 · V-24 → MB-036 · V-25 → MB-037 · V-26 → MB-038 · V-27 → MB-039 · V-28 → MB-040 · V-29 → MB-070 · V-30 → MB-071 · V-31 → MB-072 · V-32 → MB-073 · V-33 → MB-074 · V-34 → MB-075 · V-35 → MB-076 · V-36 → MB-077 · V-37 → MB-078 · V-38 → MB-079 · V-39 → MB-080 · V-40 → MB-081 · V-41 → MB-082 · V-42 → MB-083 · V-43 → MB-084 · V-44 → MB-085 · V-45 → MB-086 · V-46 → MB-087 · V-47 → MB-088 · V-48 → MB-089 · V-49 → MB-090 · V-50 → MB-091 · V-51 → MB-092 · V-52 → MB-093 · V-53 → MB-094 · V-54 → MB-095 · V-55 → MB-140 · V-56 → MB-141 · V-57 → MB-142 · V-58 → MB-143 · V-59 → MB-144 · V-60 → MB-145 · V-61 → MB-146 · V-62 → MB-185 · V-63 → MB-147 · V-64 → MB-148 · V-65 → MB-149 · V-66 → MB-150 · V-67 → MB-151 · V-68 → MB-152 · V-69 → MB-153 · V-70 → MB-154 · V-71 → MB-155 · V-72 → MB-156 · V-73 → MB-186 · V-74 → MB-187 · V-75 → MB-101 (merged with core M6)

**Core / storage / models / inference / hardware / chat (Part 4):** C1 → MB-009 · C2 → MB-010 · H1 → MB-041 · H2 → MB-042 · H3 → MB-043 · H4 → MB-044 · H5 → MB-045 · H6 → MB-046 · H7 → MB-047 · H8 → MB-048 · H9 → MB-049 · H10 → MB-050 · H11 → MB-051 · H12 → MB-052 · H13 → MB-053 · M1 → MB-096 · M2 → MB-097 · M3 → MB-098 · M4 → MB-099 · M5 → MB-100 · M6 → MB-101 (merged with V-75) · M7 → MB-102 · M8 → MB-103 · M9 → MB-104 · M10 → MB-105 · M11 → MB-106 · M12 → MB-107 · M13 → MB-108 · M14 → MB-109 · M15 → MB-110 · M16 → MB-111 · M17 → MB-112 · M18 → MB-113 · M19 → MB-114 · M20 → MB-115 · M21 → MB-116 · M22 → MB-117 · L1 → MB-157 · L2 → MB-158 · L3 → MB-159 · L4 → MB-160 · L5 → MB-161 · L6 → MB-162 · L7 → MB-163 · L8 → MB-164 · L9 → MB-165 · L10 → MB-166 · L11 → MB-167 · L12 → MB-168 · L13 → MB-169 · L14 → MB-170 · L15 → MB-171 · L16 → MB-172 · L17 → MB-173 · L18 → MB-174 · L19 → MB-175 · L20 → MB-176 · L21 → MB-177 · L22 → MB-178 · I1 → MB-188 · I2 → MB-189

**Build / CI / scripts / QML / docs / tests (Part 1):** F-01 → MB-054 · F-02 → MB-055 · F-03 → MB-056 · F-04 → MB-003 (merged with server C3) · F-05 → MB-057 · F-06 → MB-058 · F-07 → MB-118 · F-08 → MB-119 · F-09 → MB-120 · F-10 → MB-121 · F-11 → MB-122 · F-12 → MB-123 · F-13 → MB-124 · F-14 → MB-125 · F-15 → MB-126 · F-16 → MB-127 · F-17 → MB-128 · F-18 → MB-129 · F-19 → MB-130 · F-20 → MB-131 · F-21 → MB-082 (merged with V-41) · F-22 → MB-179 · F-23 → MB-047 (merged with core H7) · F-24 → MB-180 · F-25 → MB-181 · F-26 → MB-182 · F-27 → MB-184 · F-28 → MB-183

**Merges (4, reducing 193 raw enumerated findings to 189 MB findings):**

1. F-04 (docs webhook contradiction) + server C3 (unsigned webhooks in code) → MB-003.
2. F-23 (QML login URL allows any scheme) + core H7 (AccountManager accepts any URL) → MB-047.
3. F-21 (QML terminal auto-start / CodeRunner with no approval) + V-41 (QML exposure bypassing the gate) → MB-082.
4. V-75 (`PathSecurity::isInside()` unsafe) + core M6 (same helper) → MB-101. (Part 1's reference to the helper was not a numbered finding.)

**Not merged (kept separate, with cross-references, because they are different code locations / layers of the same theme):** F-05's seven self-reported issues vs the specific findings they map to (MB-003, MB-107, MB-016, MB-108/MB-062, MB-073/MB-080, MB-061, MB-025); F-19 (QML HF URL) vs MB-100 (C++ `repoId`) vs MB-010 (downloader write); F-20 (QML MCP add form) vs MB-007 (config execution) vs MB-148 (`addServerConfig`); F-02 (release manifest signing missing) vs MB-044 (placeholder key in code); F-08 (OpenSSL optional in CMake) vs MB-044; F-22 (AgentTimeline plaintext args) vs MB-025 (approval signals) vs MB-155 (export); MB-101 (`isInside()`) vs MB-178 (`sanitizeFileName()` — a distinct function in the same file, kept separate).

---

## 19. Appendix B — Severity-count reconciliation

The four part reports did not all agree with their own enumerations. This appendix records the discrepancies so the counts in §3 can be checked, rather than silently smoothing them over.

- **Part 1 (build/CI/QML/docs/tests):** 28 findings, F-01–F-28. Header tally (High 6, Medium 15, Low/Info 7) sums to 28. **Consistent.**
- **Part 2 (server):** 29 findings enumerated (C1–C3, H1–H7, M1–M11, L1–L8). The header says "Medium 10", but M1–M11 is 11 items, so the header tally sums to 28, not 29. **The enumeration (29) is used in this document; the header's Medium count is off by one.**
- **Part 3 (agent/tools/MCP/skills):** 75 findings enumerated, V-01–V-75. The header tally (5 Critical, 23 High, 27 Medium, 21 Low/Info) sums to 76, not 75. The likely cause is V-55, rated Low by its reviewer and grouped as "Medium-Low" — it appears to have been counted in both the Medium and Low/Info buckets. **The enumeration (75) is used; the Low/Info count used here is 20, not 21** (17 Low: V-55, V-56–V-61, V-63–V-72; 3 Info: V-62, V-73, V-74).
- **Part 4 (core/storage/models/inference):** The header says 58 findings (Critical 2, High 13, Medium 22, Low 19, Info 2). The enumerated list is C1–C2 (2), H1–H13 (13), M1–M22 (22), L1–L22 (22), I1–I2 (2) = **61**, not 58. **The enumeration (61) is used in this document; the header's Low count (19) is inconsistent with the 22 L-items listed.** This is the largest discrepancy, and it is why the raw total in this document is 193 (28 + 29 + 75 + 61), not the 190 that the four header tallies would suggest (28 + 29 + 75 + 58).
- **Final counts in this document:** 193 raw enumerated findings − 4 merges = **189 MB findings: Critical 10, High 48, Medium 73, Low 52, Info 6.** Every MB finding is listed in §6–§10 and mapped in Appendix A; if a count in §3 does not match a reader's hand-count of §6–§10, the hand-count of the sections is authoritative and the discrepancy should be reported as a bug in this file.

---

*End of VULNERABILITIES.md — ModelBench, commit `be192b4`, audit date 26 November 2026. Static, read-only audit. No code was executed, no exploits were run, and no fixes have been applied by writing this file.*
