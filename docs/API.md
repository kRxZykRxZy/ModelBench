# API — ModelBench Server

## Base URL

`http://localhost:3000` (or `PORT` env var)

All responses are JSON wrapped in `AppError` format on error:
```json
{ "error": "errorCode", "message": "error description" }
```

Success responses vary by endpoint.

## Authentication (`/auth`)

| Method | Endpoint | Auth | Description |
|--------|----------|------|-------------|
| `POST` | `/auth/signup` | public | Create a new user account |
| `POST` | `/auth/login` | public | Log in with email/password |
| `POST` | `/auth/logout` | bearer | Revoke the current session |
| `GET` | `/auth/me` | bearer | Get the authenticated user |

- **Mock mode**: tokens are opaque `mock-<base64>` strings; no secure storage.
- **Live mode**: Supabase access tokens verified against the auth server.

## User (`/user`)

| Method | Endpoint | Auth | Description |
|--------|----------|------|-------------|
| `GET` | `/user/profile` | bearer | Get the user's profile (tier, credits, entitlements) |
| `PUT` | `/user/profile` | bearer | Update the display name |
| `GET` | `/user/entitlements` | bearer | Get enabled features per tier |
| `GET` | `/user/credits` | bearer | Credit balance + lifetime stats |
| `GET` | `/user/credits/history` | bearer | Recent credit ledger entries |
| `GET` | `/user/usage` | bearer | Cloud AI usage events |

## AI (`/ai`)

| Method | Endpoint | Auth | Description |
|--------|----------|------|-------------|
| `GET` | `/ai/models` | none | List available OpenRouter models |
| `POST` | `/ai/estimate` | none | Estimate cost/tokens for a model |
| `GET` | `/ai/usage` | bearer | Recent cloud AI usage for the user |
| `POST` | `/ai/chat` | bearer | Proxy to OpenRouter with allowlist, cost tracking, streaming |

- **Model allowlist**: Only models in the OpenRouter catalogue that match `isAllowedModel()` are accepted. GPT/Claude/Gemini are excluded.
- **Free models**: `free: true` models cost 0 credits.
- **Cost tracking**: Each request charges credits via `recordUsage()` and returns the new balance.
- **Streaming**: SSE (`text/event-stream`) supported both in mock and live modes.

## Billing (`/billing`)

| Method | Endpoint | Auth | Description |
|--------|----------|------|-------------|
| `GET` | `/billing/plans` | none | List pricing plans |
| `POST` | `/billing/checkout` | none | Create a Whop checkout session |
| `GET` | `/billing/entitlements` | none | Look up user's tier / features / credits (uses bearer token if provided) |
| `POST` | `/billing/webhook` | none | Whop lifecycle events (HMAC-SHA256 signature verified) |
| `GET` | `/billing/mock-success` | none | Mock checkout completion page |

- **Webhook signature**: `X-Whop-Signature` or `webhook-signature: v1,<base64>` header. Verified using `crypto.timingSafeEqual` in `safeEqual()` to prevent timing attacks. Secret from `WHOP_WEBHOOK_SECRET`.
- **Idempotency**: Whop retries aggressively; duplicate event IDs are acknowledged with `{ ok: true, duplicate: true }`.

## Error Codes

| Code | Meaning |
|------|---------|
| 400 | Bad request — invalid input, model not allowed, etc. |
| 401 | Unauthorized — missing or invalid token |
| 402 | Insufficient credits — not enough balance for the operation |
| 403 | Forbidden — not allowed to perform the action |
| 404 | Not found — route or resource not found |
| 500 | Internal server error |
| 503 | Service not configured — missing API key or env var |

## Environment Variables

| Variable | Required? | Description |
|----------|-----------|-------------|
| `PORT` | no | Server port (default 3000) |
| `SITE_URL` | no | Site URL for redirect callbacks |
| `SUPABASE_URL` | yes (live) | Supabase project URL |
| `SUPABASE_ANON_KEY` | yes (live) | Supabase anon key |
| `SUPABASE_SERVICE_ROLE_KEY` | yes (live) | Supabase service role key |
| `WHOP_API_KEY` | yes (live) | Whop API key |
| `WHOP_WEBHOOK_SECRET` | yes (live) | Whop webhook HMAC secret |
| `WHOP_PLAN_PRO`, `WHOP_PLAN_ULTIMATE`, `WHOP_PLAN_TEAMS` | yes (live) | Whop plan IDs per tier |
| `OPENROUTER_API_KEY` | yes (live) | OpenRouter API key |
| `AI_MOCK` | no | Force mock mode (`true`/`false`) |
| `ALLOW_UNSIGNED_WEBHOOKS` | no | Allow webhooks without signature (dev) |
| `CORS_ORIGINS` / `ALLOWED_ORIGINS` | no | Comma-separated allowlist (overrides defaults) |
| `RATE_LIMIT_DISABLED` | no | Disable all rate limits |