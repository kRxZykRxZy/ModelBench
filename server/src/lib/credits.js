import { supabaseAdmin, isConfigured } from './supabase.js';
import * as mock from './mock.js';
import {
  CREDITS_PER_USD,
  creditsForTier,
  featuresForTier,
  getPlan,
  normalizeTier
} from '../config/pricing.js';
import { AppError, insufficientCredits } from './errors.js';

export const LOW_BALANCE_THRESHOLD = Number(process.env.LOW_BALANCE_THRESHOLD || 1);

/**
 * Make sure a profile row exists. Supabase's `handle_new_user` trigger normally
 * creates it; this is a safety net for projects that installed the schema after
 * users signed up (and for mock mode).
 */
export async function ensureProfile(user) {
  if (!user?.id) throw new AppError(400, 'missing_user', 'user id required');

  if (!isConfigured()) {
    return mock.getOrCreateProfile(user.id, user.email);
  }

  const { error: upsertError } = await supabaseAdmin
    .from('profiles')
    .upsert(
      { id: user.id, email: user.email || null },
      { onConflict: 'id', ignoreDuplicates: true }
    );
  if (upsertError) {
    throw new AppError(500, 'profile_error', upsertError.message);
  }

  const { data, error } = await supabaseAdmin
    .from('profiles')
    .select('*')
    .eq('id', user.id)
    .maybeSingle();
  if (error) throw new AppError(500, 'profile_error', error.message);
  return data;
}

export async function getProfile(userId) {
  if (!isConfigured()) return mock.getProfile(userId) || mock.getOrCreateProfile(userId);
  const { data, error } = await supabaseAdmin
    .from('profiles')
    .select('*')
    .eq('id', userId)
    .maybeSingle();
  if (error) throw new AppError(500, 'profile_error', error.message);
  return data;
}

export async function getBalance(userId) {
  const profile = await getProfile(userId);
  return Number(profile?.credits_balance || 0);
}

/**
 * Grant credits and append a ledger entry.
 * @returns {Promise<{balance:number}>}
 */
export async function grantCredits(userId, amount, reason = 'grant', metadata = {}) {
  const value = Number(amount);
  if (!Number.isFinite(value) || value <= 0) {
    throw new AppError(400, 'invalid_amount', 'amount must be positive');
  }

  if (!isConfigured()) {
    return mock.grantCredits(userId, value, reason, metadata);
  }

  const { data, error } = await supabaseAdmin.rpc('grant_credits', {
    p_user_id: userId,
    p_amount: value,
    p_reason: reason,
    p_metadata: metadata || {}
  });
  if (error) throw mapCreditError(error);
  return { balance: Number(data) };
}

/**
 * Deduct credits. Throws 402 `insufficient_credits` if the balance is too low.
 * @returns {Promise<{balance:number}>}
 */
export async function deductCredits(userId, amount, reason = 'deduction', metadata = {}) {
  const value = Number(amount);
  if (!Number.isFinite(value) || value < 0) {
    throw new AppError(400, 'invalid_amount', 'amount must not be negative');
  }

  if (!isConfigured()) {
    return mock.deductCredits(userId, value, reason, metadata);
  }

  const { data, error } = await supabaseAdmin.rpc('deduct_credits', {
    p_user_id: userId,
    p_amount: value,
    p_reason: reason,
    p_metadata: metadata || {}
  });
  if (error) throw mapCreditError(error);
  return { balance: Number(data) };
}

/**
 * Charge a completed cloud AI request and persist the usage event.
 * @returns {Promise<{balance:number, charged:number}>}
 */
export async function recordUsage({
  userId,
  model,
  provider = null,
  promptTokens = 0,
  completionTokens = 0,
  costUsd = 0,
  credits = 0,
  requestId = null,
  status = 'ok',
  metadata = {}
}) {
  const charged = Math.max(0, Number(credits) || 0);
  const cost = Math.max(0, Number(costUsd) || 0);

  if (!isConfigured()) {
    const result = mock.recordUsage({
      userId,
      model,
      provider,
      promptTokens,
      completionTokens,
      costUsd: cost,
      credits: charged,
      requestId,
      status,
      metadata
    });
    return { balance: result.balance, charged: result.charged };
  }

  const { data, error } = await supabaseAdmin.rpc('record_usage', {
    p_user_id: userId,
    p_model: model,
    p_prompt_tokens: Math.max(0, Math.trunc(Number(promptTokens) || 0)),
    p_completion_tokens: Math.max(0, Math.trunc(Number(completionTokens) || 0)),
    p_cost_usd: cost,
    p_credits: charged,
    p_request_id: requestId,
    p_provider: provider,
    p_status: status,
    p_metadata: metadata || {}
  });
  if (error) throw mapCreditError(error);
  return {
    balance: Number(data?.balance ?? 0),
    charged: Number(data?.charged ?? charged)
  };
}

export async function listCreditHistory(userId, limit = 50) {
  const safeLimit = clampLimit(limit);
  if (!isConfigured()) return mock.listLedger(userId, safeLimit);
  const { data, error } = await supabaseAdmin
    .from('credit_ledger')
    .select('*')
    .eq('user_id', userId)
    .order('created_at', { ascending: false })
    .limit(safeLimit);
  if (error) throw new AppError(500, 'ledger_error', error.message);
  return data || [];
}

export async function listUsage(userId, limit = 50) {
  const safeLimit = clampLimit(limit);
  if (!isConfigured()) return mock.listUsage(userId, safeLimit);
  const { data, error } = await supabaseAdmin
    .from('usage_events')
    .select('*')
    .eq('user_id', userId)
    .order('created_at', { ascending: false })
    .limit(safeLimit);
  if (error) throw new AppError(500, 'usage_error', error.message);
  return data || [];
}

export async function getUsageTotals(userId) {
  if (!isConfigured()) return mock.usageTotals(userId);
  // The profile already tracks lifetime credits. For token/cost totals we use a
  // bounded recent window (dashboards only need the recent picture).
  const events = await listUsage(userId, 200);
  return events.reduce((acc, entry) => {
    acc.requests += 1;
    acc.prompt_tokens += Number(entry.prompt_tokens) || 0;
    acc.completion_tokens += Number(entry.completion_tokens) || 0;
    acc.cost_usd = round(acc.cost_usd + (Number(entry.cost_usd) || 0), 8);
    acc.credits_charged = round(acc.credits_charged + (Number(entry.credits_charged) || 0), 6);
    return acc;
  }, { requests: 0, prompt_tokens: 0, completion_tokens: 0, cost_usd: 0, credits_charged: 0 });
}

/** Full payload for GET /user/credits. */
export async function getCreditSummary(userId, { historyLimit = 10 } = {}) {
  const [profile, recentLedger, usageTotals] = await Promise.all([
    getProfile(userId),
    listCreditHistory(userId, historyLimit),
    getUsageTotals(userId)
  ]);

  const tier = normalizeTier(profile?.tier) || 'free';
  const plan = getPlan(tier);
  const balance = Number(profile?.credits_balance || 0);

  return {
    tier,
    plan: plan ? { tier: plan.tier, name: plan.name, priceUsd: plan.priceUsd, credits: plan.credits } : null,
    balance: round(balance, 6),
    credits_balance: round(balance, 6),
    credits_per_usd: CREDITS_PER_USD,
    low_balance: balance < LOW_BALANCE_THRESHOLD,
    low_balance_threshold: LOW_BALANCE_THRESHOLD,
    lifetime: {
      granted: Number(profile?.credits_lifetime_granted || 0),
      used: Number(profile?.credits_lifetime_used || 0)
    },
    subscription: {
      status: profile?.subscription_status || (tier === 'free' ? 'inactive' : 'active'),
      current_period_end: profile?.current_period_end || null,
      membership_id: profile?.whop_membership_id || null
    },
    usage_totals: usageTotals,
    recent_ledger: recentLedger,
    mock: !isConfigured()
  };
}

export async function syncEntitlements(userId, tier) {
  const normalized = normalizeTier(tier) || 'free';
  const features = featuresForTier(normalized);

  if (!isConfigured()) {
    mock.updateProfile(userId, { tier: normalized });
    return features;
  }

  const { error } = await supabaseAdmin.rpc('sync_entitlements', {
    p_user_id: userId,
    p_features: features,
    p_source: 'plan'
  });
  if (error) throw new AppError(500, 'entitlements_error', error.message);
  return features;
}

export async function getEntitlements(userId) {
  if (!isConfigured()) {
    const profile = mock.getProfile(userId) || mock.getOrCreateProfile(userId);
    return featuresForTier(profile.tier).map((feature) => ({
      feature,
      enabled: true,
      source: 'plan'
    }));
  }

  const { data, error } = await supabaseAdmin
    .from('entitlements')
    .select('feature, enabled, source, expires_at')
    .eq('user_id', userId);
  if (error) throw new AppError(500, 'entitlements_error', error.message);

  if (data && data.length > 0) return data;
  const profile = await getProfile(userId);
  return featuresForTier(profile?.tier).map((feature) => ({
    feature,
    enabled: true,
    source: 'plan'
  }));
}

/** Credits granted by a plan. Exposed for billing/docs. */
export function planCredits(tier) {
  return creditsForTier(tier);
}

function clampLimit(limit) {
  const value = Number(limit);
  if (!Number.isFinite(value)) return 50;
  return Math.max(1, Math.min(Math.trunc(value), 200));
}

function round(value, decimals) {
  const factor = 10 ** decimals;
  return Math.round(value * factor) / factor;
}

function mapCreditError(error) {
  const message = error?.message || 'credit operation failed';
  if (/insufficient_credits/i.test(message)) {
    const match = /balance=([\d.]+) required=([\d.]+)/i.exec(message);
    return insufficientCredits(match ? Number(match[1]) : 0, match ? Number(match[2]) : null);
  }
  if (/profile_not_found/i.test(message)) {
    return new AppError(404, 'profile_not_found', 'Profile not found');
  }
  if (/function .* does not exist|Could not find the function/i.test(message)) {
    return new AppError(
      500,
      'schema_not_installed',
      'Credit functions are missing. Run server/src/lib/schema.sql in Supabase.'
    );
  }
  return new AppError(500, 'credits_error', message);
}
