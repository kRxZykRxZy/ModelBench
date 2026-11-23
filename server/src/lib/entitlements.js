import { supabaseAdmin, isConfigured } from './supabase.js';
import * as mock from './mock.js';
import {
  getPlan,
  normalizeTier,
  tierForPlanId
} from '../config/pricing.js';
import {
  ensureProfile,
  getProfile,
  grantCredits,
  syncEntitlements
} from './credits.js';
import { AppError } from './errors.js';

/** YYYY-MM bucket used to make credit grants idempotent per billing period. */
export function currentPeriodKey(date = new Date()) {
  return date.toISOString().slice(0, 7);
}

/**
 * Apply an active/upgraded plan to a user.
 *
 * Credit grants are period-aware:
 *   - first activation / new month  -> grant the full plan credit amount
 *   - upgrade within the same month -> grant only the difference
 *   - downgrade / retry             -> grant nothing
 * This makes webhook retries harmless and prevents double-granting when Whop
 * sends both `membership_activated` and `payment_succeeded`.
 */
export async function activatePlan({
  userId,
  tier,
  membershipId = null,
  planId = null,
  periodEnd = null,
  status = 'active',
  periodKey = currentPeriodKey(),
  eventType = null,
  metadata = {}
}) {
  const normalizedTier = normalizeTier(tier);
  if (!normalizedTier || normalizedTier === 'free') {
    throw new AppError(400, 'invalid_tier', `unknown paid tier: ${tier}`);
  }

  const profile = await ensureProfile({ id: userId });
  const oldTier = normalizeTier(profile?.tier) || 'free';
  const oldPeriodKey = profile?.credit_period_key || null;
  const newPlan = getPlan(normalizedTier);
  const oldPlan = getPlan(oldTier);

  let creditsToGrant = 0;
  if (oldPeriodKey !== periodKey) {
    creditsToGrant = newPlan.credits;
  } else if (normalizedTier !== oldTier && newPlan.credits > oldPlan.credits) {
    creditsToGrant = roundCredits(newPlan.credits - oldPlan.credits);
  }

  const patch = {
    tier: normalizedTier,
    subscription_status: status,
    whop_membership_id: membershipId || profile?.whop_membership_id || null,
    current_period_end: periodEnd || profile?.current_period_end || null,
    credit_period_key: periodKey,
    ...(creditsToGrant > 0 ? { last_credit_at: new Date().toISOString() } : {})
  };

  if (isConfigured()) {
    const { error } = await supabaseAdmin
      .from('profiles')
      .update(patch)
      .eq('id', userId);
    if (error) throw new AppError(500, 'profile_error', error.message);
  } else {
    mock.updateProfile(userId, patch);
  }

  let balance = Number(profile?.credits_balance || 0);
  if (creditsToGrant > 0) {
    const result = await grantCredits(
      userId,
      creditsToGrant,
      `plan_${normalizedTier}_${periodKey}`,
      {
        source: 'whop',
        event_type: eventType,
        plan_id: planId,
        membership_id: membershipId,
        tier: normalizedTier,
        ...metadata
      }
    );
    balance = result.balance;
  }

  await syncEntitlements(userId, normalizedTier);
  await upsertSubscription({
    id: membershipId || `whop_${userId}`,
    userId,
    planId,
    tier: normalizedTier,
    status,
    periodEnd,
    periodKey,
    cancelAtPeriodEnd: false,
    metadata
  });

  return {
    userId,
    tier: normalizedTier,
    granted: creditsToGrant,
    balance,
    periodKey,
    status
  };
}

/** Cancel/expire a subscription. Remaining credits stay usable. */
export async function deactivatePlan({
  userId,
  membershipId = null,
  status = 'canceled',
  periodEnd = null,
  eventType = null,
  metadata = {}
}) {
  const patch = {
    tier: 'free',
    subscription_status: status,
    current_period_end: periodEnd || null,
    whop_membership_id: membershipId || undefined
  };
  // Avoid writing undefined explicitly.
  if (patch.whop_membership_id === undefined) delete patch.whop_membership_id;

  if (isConfigured()) {
    const { error } = await supabaseAdmin.from('profiles').update(patch).eq('id', userId);
    if (error) throw new AppError(500, 'profile_error', error.message);
  } else {
    mock.updateProfile(userId, patch);
  }

  await syncEntitlements(userId, 'free');
  await upsertSubscription({
    id: membershipId || `whop_${userId}`,
    userId,
    tier: 'free',
    status,
    periodEnd,
    cancelAtPeriodEnd: true,
    metadata
  });

  return { userId, tier: 'free', status };
}

/** Keep the tier active during Whop's payment-retry grace period. */
export async function markPaymentFailed({ userId, membershipId = null, eventType = null, metadata = {} }) {
  const patch = { subscription_status: 'past_due' };
  if (isConfigured()) {
    const { error } = await supabaseAdmin.from('profiles').update(patch).eq('id', userId);
    if (error) throw new AppError(500, 'profile_error', error.message);
  } else {
    mock.updateProfile(userId, patch);
  }
  await upsertSubscription({
    id: membershipId || `whop_${userId}`,
    userId,
    tier: (await getProfile(userId))?.tier || 'free',
    status: 'past_due',
    metadata: { ...metadata, event_type: eventType }
  });
  return { userId, status: 'past_due' };
}

/**
 * Flatten the different Whop payload shapes into one object.
 * Whop currently sends `{action, data}`; older integrations send `{type, data}`.
 */
export function normalizeWhopEvent(event = {}) {
  const type = String(event.type || event.action || event.event || event.event_type || '').toLowerCase();
  const data = (event.data && typeof event.data === 'object') ? event.data : event;
  const metadata = {
    ...(event.metadata && typeof event.metadata === 'object' ? event.metadata : {}),
    ...(data.metadata && typeof data.metadata === 'object' ? data.metadata : {})
  };
  const membership = data.membership && typeof data.membership === 'object' ? data.membership : {};
  const plan = data.plan && typeof data.plan === 'object' ? data.plan : {};

  const userId =
    metadata.user_id || metadata.userId || metadata.supabase_user_id ||
    data.user_id || data.userId ||
    membership.user_id || membership.userId ||
    event.user_id ||
    null;

  const email =
    metadata.email || data.email ||
    (data.user && data.user.email) ||
    (membership.user && membership.user.email) ||
    null;

  const planId =
    data.plan_id || data.planId ||
    plan.id || plan.plan_id ||
    membership.plan_id ||
    event.plan_id ||
    null;

  const planName = plan.name || data.plan_name || membership.plan_name || null;

  const membershipId =
    data.membership_id || data.membershipId ||
    membership.id ||
    data.id ||
    event.membership_id ||
    null;

  const periodEnd = toIso(
    data.current_period_end || data.renewal_period_end || data.expires_at ||
    data.valid_until || membership.expires_at || membership.valid_until ||
    null
  );

  const periodStart = toIso(data.period_start || data.current_period_start || membership.created_at || null);

  const status = data.status || membership.status || data.membership_status || null;

  return {
    type,
    data,
    metadata,
    userId,
    email,
    planId,
    planName,
    membershipId,
    periodEnd,
    periodStart,
    status,
    tier: tierForPlanId(planId) || tierForPlanId(planName) || normalizeTier(metadata.tier) || null
  };
}

/** Resolve the ModelBench user referenced by a webhook. */
export async function resolveWebhookUser(normalized) {
  let userId = normalized.userId;

  if (userId && !isUuid(userId)) {
    // Some integrations pass the email as user_id; accept that in mock mode.
    if (String(userId).includes('@')) {
      normalized.email = normalized.email || userId;
      userId = null;
    }
  }

  if (!userId && normalized.email) {
    if (!isConfigured()) {
      const user = mock.findUserByEmail(normalized.email);
      userId = user?.id || null;
    } else {
      const { data, error } = await supabaseAdmin
        .from('profiles')
        .select('id')
        .ilike('email', normalized.email)
        .limit(1)
        .maybeSingle();
      if (error) throw new AppError(500, 'profile_error', error.message);
      userId = data?.id || null;
    }
  }

  return userId || null;
}

/**
 * Apply a Whop event. Returns a small result object used by the route.
 * Unknown events are acknowledged (200) so Whop stops retrying them.
 */
export async function handleWhopEvent(event) {
  const normalized = normalizeWhopEvent(event);
  const type = normalized.type;
  const userId = await resolveWebhookUser(normalized);

  if (!type) return { handled: false, reason: 'missing_event_type' };
  if (!userId) return { handled: false, reason: 'user_not_found', type };

  const isCancel = /(deactivat|cancel|expire|ended|went_invalid|refunded)/.test(type)
    || /membership_deactivated/.test(type);
  const isFailure = /(payment|invoice|charge).*(failed|declined|void)/.test(type)
    || /payment_failed/.test(type);
  const isActivation = /(activat|created|went_valid|renew|resumed|updated|changed)/.test(type)
    || /(payment|invoice|charge).*(succeeded|paid|completed)/.test(type)
    || /checkout.*(completed|succeeded)/.test(type);

  if (isCancel) {
    const result = await deactivatePlan({
      userId,
      membershipId: normalized.membershipId,
      status: normalized.status || 'canceled',
      periodEnd: normalized.periodEnd,
      eventType: type,
      metadata: { plan_id: normalized.planId, source: 'whop' }
    });
    return { handled: true, action: 'canceled', ...result, type };
  }

  if (isFailure) {
    const result = await markPaymentFailed({
      userId,
      membershipId: normalized.membershipId,
      eventType: type,
      metadata: { plan_id: normalized.planId, source: 'whop' }
    });
    return { handled: true, action: 'payment_failed', ...result, type };
  }

  if (isActivation) {
    if (!normalized.tier || normalized.tier === 'free') {
      return { handled: false, reason: 'unknown_plan', type, planId: normalized.planId };
    }
    const periodKey = normalized.periodStart
      ? currentPeriodKey(new Date(normalized.periodStart))
      : currentPeriodKey();
    const result = await activatePlan({
      userId,
      tier: normalized.tier,
      membershipId: normalized.membershipId,
      planId: normalized.planId,
      periodEnd: normalized.periodEnd,
      status: normalized.status || 'active',
      periodKey,
      eventType: type,
      metadata: { source: 'whop' }
    });
    return { handled: true, action: 'activated', ...result, type };
  }

  return { handled: false, reason: 'ignored_event', type };
}

async function upsertSubscription({
  id,
  userId,
  planId = null,
  tier,
  status = 'active',
  periodEnd = null,
  periodKey = null,
  cancelAtPeriodEnd = false,
  metadata = {}
}) {
  const row = {
    id,
    user_id: userId,
    plan_id: planId,
    tier,
    status,
    current_period_end: periodEnd,
    cancel_at_period_end: cancelAtPeriodEnd,
    last_credit_period: periodKey,
    metadata
  };

  if (!isConfigured()) {
    mock.upsertSubscription(row);
    return row;
  }

  const { error } = await supabaseAdmin
    .from('subscriptions')
    .upsert(row, { onConflict: 'id' });
  if (error) throw new AppError(500, 'subscription_error', error.message);
  return row;
}

function toIso(value) {
  if (value === null || value === undefined || value === '') return null;
  if (typeof value === 'number' || /^\d+$/.test(String(value))) {
    const numeric = Number(value);
    const ms = numeric < 1e12 ? numeric * 1000 : numeric;
    const date = new Date(ms);
    return Number.isNaN(date.getTime()) ? null : date.toISOString();
  }
  const date = new Date(value);
  return Number.isNaN(date.getTime()) ? null : date.toISOString();
}

function isUuid(value) {
  return /^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i.test(String(value));
}

function roundCredits(value) {
  return Math.round((Number(value) || 0) * 1e6) / 1e6;
}
