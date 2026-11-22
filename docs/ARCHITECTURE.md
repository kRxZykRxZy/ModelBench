# Architecture

See master build prompt §4. Key decisions:

- Qt 6.12 LTS, C++20, CMake, SQLite (WAL).
- Modular layout under `app/`: core, inference, hardware, models, chat, mcp, skills, billing, api, storage, platform, ui.
- Secrets never in SQLite/QML/JSON/logs; use `ISecretStorage` (DPAPI on Windows, Keychain on macOS).
- Inference via `libllama` behind `IInferenceBackend`; `HAVE_LLAMA_CPP` gates real backend.
- MCP: stdio (JSON-RPC over stdout, logs on stderr) + Streamable HTTP.
- Skills: skills.sh client + local manager parsing SKILL.md.
- Billing: client mints checkout via secure backend; no secrets in binary.

## Module ownership
- Main agent: core, models, chat, billing, api, storage, integration, docs.
- Subagent 1: inference, hardware.
- Subagent 2: mcp, skills.
- Subagent 3: ui, platform UI.
