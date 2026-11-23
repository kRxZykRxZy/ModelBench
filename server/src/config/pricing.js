/**
 * ModelBench plan + credit pricing.
 *
 * Credits are deliberately simple: 1 credit == 1 USD of cloud AI usage.
 * Paid plans grant credits equal to their monthly price, so a $7 Hobby plan
 * comes with 7 credits (i.e. $7 of OpenRouter usage). Local inference never
 * consumes credits.
 *
 * Prices are hardcoded on purpose. Do not fetch them from Whop at runtime:
 * the checkout URL is created by Whop, but entitlements must be deterministic
 * and auditable even if the billing provider is unavailable.
 */

export const CREDITS_PER_USD = 1;

/** Plan catalogue. Keep this in sync with docs/BILLING.md and the desktop UI. */
export const PLANS = Object.freeze({
  free: {
    tier: 'free',
    name: 'Free',
    priceUsd: 0,
    credits: 0,
    interval: 'month',
    features: ['inference', 'chat', 'models', 'mcp', 'skills'],
    description: 'Unlimited local inference with your own models.'
  },
  hobby: {
    tier: 'hobby',
    name: 'Hobby',
    priceUsd: 7,
    credits: 7,
    interval: 'month',
    features: ['inference', 'chat', 'models', 'mcp', 'skills', 'cloud_ai', 'cloud_sync'],
    description: '$7 of cloud AI credits per month.'
  },
  scale: {
    tier: 'scale',
    name: 'Scale',
    priceUsd: 25,
    credits: 25,
    interval: 'month',
    features: ['inference', 'chat', 'models', 'mcp', 'skills', 'cloud_ai', 'cloud_sync', 'priority_support', 'advanced'],
    description: '$25 of cloud AI credits per month.'
  },
  teams: {
    tier: 'teams',
    name: 'Teams',
    priceUsd: 149,
    credits: 149,
    interval: 'month',
    features: ['inference', 'chat', 'models', 'mcp', 'skills', 'cloud_ai', 'cloud_sync', 'priority_support', 'advanced', 'team_seats'],
    description: '$149 of cloud AI credits per month for teams.'
  }
});

/** Legacy / friendly aliases accepted by the API. */
export const TIER_ALIASES = Object.freeze({
  pro: 'hobby',
  starter: 'hobby',
  basic: 'hobby',
  ultimate: 'scale',
  premium: 'scale',
  business: 'teams',
  team: 'teams'
});

/** Environment variable that stores the Whop plan id for each tier. */
export const PLAN_ENV = Object.freeze({
  hobby: ['WHOP_PLAN_HOBBY', 'WHOP_PLAN_PRO'],
  scale: ['WHOP_PLAN_SCALE', 'WHOP_PLAN_ULTIMATE'],
  teams: ['WHOP_PLAN_TEAMS']
});

export function normalizeTier(input) {
  if (!input) return 'free';
  const raw = String(input).trim().toLowerCase();
  const tier = TIER_ALIASES[raw] || raw;
  return Object.prototype.hasOwnProperty.call(PLANS, tier) ? tier : null;
}

export function getPlan(input) {
  const tier = normalizeTier(input);
  return tier ? PLANS[tier] : null;
}

export function isValidTier(input) {
  return normalizeTier(input) !== null;
}

export function creditsForTier(input) {
  const plan = getPlan(input);
  return plan ? plan.credits : 0;
}

export function priceForTier(input) {
  const plan = getPlan(input);
  return plan ? plan.priceUsd : 0;
}

export function featuresForTier(input) {
  const plan = getPlan(input);
  return plan ? [...plan.features] : [...PLANS.free.features];
}

/** Resolve the configured Whop plan id for a tier (env vars win). */
export function planIdForTier(input) {
  const tier = normalizeTier(input);
  if (!tier || tier === 'free') return null;
  for (const key of PLAN_ENV[tier] || []) {
    const value = process.env[key];
    if (value) return value;
  }
  return null;
}

/** Reverse lookup: map a Whop plan id (or a plan name) back to a tier. */
export function tierForPlanId(input) {
  if (!input) return null;
  const raw = String(input).trim();
  if (!raw) return null;

  // Exact match against configured ids.
  for (const [tier, keys] of Object.entries(PLAN_ENV)) {
    for (const key of keys) {
      if (process.env[key] && process.env[key] === raw) return tier;
    }
  }

  // Common id/name shapes: plan_hobby, price_hobby_monthly, "Hobby Plan".
  const lowered = raw.toLowerCase();
  const alias = lowered.replace(/^(plan|price|prod|product)[_-]/, '').replace(/[_-](month|monthly|year|yearly|annual).*$/, '');
  const normalized = normalizeTier(alias);
  if (normalized) return normalized;

  for (const tier of Object.keys(PLANS)) {
    if (lowered === tier || lowered.includes(tier)) return tier;
  }
  return null;
}

export function plansList() {
  return Object.values(PLANS).map((plan) => ({ ...plan, features: [...plan.features] }));
}
