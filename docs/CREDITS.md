# Credits

Credits are the currency for cloud AI inference. Local inference is always free.

## Balance and usage

- `GET /user/credits` returns the balance, lifetime granted/used, tier, plan and
  recent ledger.
- `GET /user/usage` returns recent cloud AI usage events and totals.
- `POST /ai/chat` estimates the cost, checks the balance, runs the request and
  deducts credits on completion (`recordUsage`).

## Deduction flow

1. The client estimates credits for the requested model and token budget.
2. The server checks `credits_balance >= estimatedCredits`; otherwise it returns
   `402 insufficient_credits`.
3. The request runs (mock or OpenRouter).
4. On completion the server calls `recordUsage`, which atomically deducts the
   charged credits and appends a ledger entry.
5. The response includes `modelbench.credits_charged` and `modelbench.balance`.

## Mock mode

When Supabase/OpenRouter are not configured, the server runs in mock mode:
profiles and credits are kept in memory, and `/ai/chat` returns a mock response
while still deducting credits. This enables local development and integration
tests without external services.

## Low balance

The UI (`CreditsView`, sidebar) reads the `credits` context property and shows a
warning when the balance is below the threshold (default 1 credit unit).

## Plans

Paid plans grant credits equal to their monthly price (see `config/pricing.js`).
Upgrades are handled via Whop checkout and the webhook handler.
