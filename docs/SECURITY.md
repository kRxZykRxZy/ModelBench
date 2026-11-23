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

## Known Security Issues

1. **Billing webhook signature verification NOT implemented** (CRITICAL)
   - The `/billing/webhook` route returns `{ok: true}` without verifying the
     `X-Whop-Signature` header using HMAC-SHA256 with the webhook secret.
   - An attacker can forge webhook requests to manipulate entitlements/tiers.
   - Fix: verify `req.headers['x-whop-signature']` against the raw body using
     `crypto.createHmac('sha256', process.env.WHOP_WEBHOOK_SECRET).update(body).digest('hex')`.

2. **No cross-platform secret storage**
   - Windows: DPAPI via `CryptProtectData` / `CryptUnprotectData` is used,
     but macOS and Linux have no implementation (store/load return false/{}).
   - Fix: add Keychain Services (macOS) and libsecret (Linux) support.

3. **CORS allows all origins with credentials**
   - `origin: (origin, cb) => cb(null, true)` permits any website to make
     authenticated requests to the local API if the user's session is active.
   - Fix: restrict to specific origins or disable `credentials` with open CORS.

4. **Auth token stored in memory only**
   - `AccountManager.m_accessToken` is held in RAM; no secure storage (keychain/
     DPAPI-encrypted file) for persistence across sessions.
   - Tokens could be exposed in crash dumps or memory forensics.
   - Fix: persist tokens in encrypted secret storage.

5. **MCP tool approval gate is async/UI-dependent**
   - The approval gate relies on external UI calling `approveToolCall(id, true)`;
     there is no server-side enforcement timeout or fallback.
   - Fix: add timeout auto-denial and server-side call filtering.

6. **No rate limiting on auth/webhook endpoints**
   - Brute-force or replay attacks are undeterred.
   - Fix: add rate limiting (e.g., express-rate-limit).

7. **Secret arguments not masked in approval UI preview**
   - Tool arguments containing API keys, passwords, etc., are passed in plaintext
     to the approval dialog.
   - Fix: mask/sensitive-value redaction in approval preview.
