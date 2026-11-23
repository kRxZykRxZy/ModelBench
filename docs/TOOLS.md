# Tools

Tools are registered in a `ToolRegistry` and executed through an approval gate.
Each tool implements `Tool::execute(callId, args)` and emits `finished` exactly
once. Results are delivered as `ToolResult` (ok, output, error, duration, …).

## Built-in tools

### File system (`FileSystemTools`)

- `read_file` — read a file (size-capped).
- `write_file` — create/overwrite a file (write approval).
- `edit_file` — exact text replacement (write approval).
- `list_dir` — list directory contents.
- `search_code` — search files for a pattern.

All paths are sandboxed to the configured root; attempts to escape are rejected.

### Terminal (`TerminalTool`)

- `run_terminal` — run a command with cwd, timeout, env and streaming output.
  Risk level is `Execute` (requires approval unless auto-approved).

### Git (`GitTools`)

See `GIT.md`. Seven tools: `git_status`, `git_diff`, `git_log`, `git_branch`,
`git_commit`, `git_push`, `git_pull`.

### MCP tools

`MCPClient` discovers tools from an MCP server and exposes them through the same
approval gate. `ToolCalling` validates arguments against the server's input schema
before dispatching.

## Validation

`ToolRegistry::validateArgs` checks `required`, `type` (string/number/integer/
boolean/object/array) and `enum`. `ToolCalling::validateArgs` mirrors this for
MCP tools.

## Timeouts and output caps

The registry runs a per-call watchdog (`defaultTimeoutMs`, default 120 s).
Output is capped at `maxOutputBytes` (default 1 MiB) and marked `truncated`.

## Undo

`FileEditHistory` records file writes/edits and supports `undoLast()` for the
session.
