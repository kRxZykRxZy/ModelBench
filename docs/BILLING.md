# Billing — ModelBench

## Plans & Prices

| Tier | Price (USD) | Credits | Features |
|------|-------------|---------|----------|
| **free** | $0 | 0 | Unlimited local inference with your own models |
| **hobby** | $7 | 7 | $7 of cloud AI credits per month |
| **scale** | $25 | 25 | $25 of cloud AI credits per month |
| **teams** | $149 | 149 | $149 of cloud AI credits per month for teams |

*1 credit = 1 USD of OpenRouter usage.*

## Credit Model

- Credits are granted when a paid plan is activated via Whop webhook.
- Credits are deducted per cloud AI request (`1 credit = 1 USD` of OpenRouter usage).
- Each plan's monthly credit allowance resets per billing period.
- The `credit_period_key` (YYYY-MM) on the profiles table prevents double-granting when webhooks are retried.

## Webhook Signature Verification

The `/billing/webhook` endpoint verifies Whop webhooks using HMAC-SHA256.

- **Header**: `X-Whop-Signature` (legacy) or `webhook-signature: v1,<base64>` (standard)
- **Secret**: `WHOP_WEBHOOK_SECRET` environment variable
- **Verification**: `crypto.timingSafeEqual` is used to compare the computed HMAC against the received signature in `safeEqual()` — this prevents timing attacks.

If the secret is not configured (mock mode), unsigned webhooks are accepted for local development.

## Local Development (Mock Mode)

Set no env vars to run in mock mode:

- `SUPABASE_URL`, `SUPABASE_ANON_KEY`, `SUPABASE_SERVICE_ROLE_KEY` → absent → Supabase mock
- `WHOP_API_KEY` → absent → Whop mock
- `OPENROUTER_API_KEY` → absent → OpenRouter mock
- `AI_MOCK=true` → forces mock mode for AI calls

In mock mode, credits are granted instantly on checkout, and AI calls return synthetic responses with cost tracking.

## API Endpoints

- `GET /billing/plans` — public pricing catalogue
- `POST /billing/checkout` — create a Whop checkout session
- `GET /billing/entitlements` — look up user's tier / features / credits
- `POST /billing/webhook` — Whop lifecycle events (subscription activated, payment failed, etc.)
- `GET /billing/mock-success` — mock checkout completion page