# Security

- Secrets stored via OS secure storage (DPAPI/Credential Manager on Windows, Keychain on macOS). Never in SQLite, QML, JSON, logs, or crash reports.
- MCP servers require explicit install approval; tool calls require permission levels; sensitive tools require confirmation with argument preview.
- Tool output treated as untrusted; never overwrites system instructions.
- Skills never auto-execute scripts; inspect before install.
- Model files: size/disk checks; licence display; no code execution from repos.
- Local API binds to localhost by default; optional key.
- All network uses HTTPS; no TLS verification bypass.
- Logs redact tokens/keys/headers/payment info.
- Path traversal prevention on all file operations.
