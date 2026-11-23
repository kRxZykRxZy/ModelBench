/**
 * In-memory storage used when Supabase credentials are absent.
 *
 * The mock store intentionally mirrors the SQL schema (profiles, ledger,
 * usage_events, subscriptions) so the rest of the server can use the same code
 * paths in development and production. Data is process-local and resets when
 * the server restarts.
 */
import crypto from 'crypto';
import {
  CREDITS_PER_USD,
  featuresForTier,
  getPlan,
  normalizeTier
} from '../config/pricing.js';
import { AppError, insufficientCredits } from './errors.js';

const usersByEmail = new Map();
const usersById = new Map();
const profiles = new Map();
const ledger = [];
const usage = [];
const subscriptions = new Map();
const webhookEvents = new Set();
// Opaque mock sessions. Never derive tokens from user data: predictable tokens
// would let anyone impersonate a local account.
const tokens = new Map();

let ledgerSeq = 1;
let usageSeq = 1;

function nowIso() {
  return new Date().toISOString();
}

function normalizeEmail(email) {
  return String(email || '').trim().toLowerCase();
}

function userIdForEmail(email) {
  return `mock_${crypto.createHash('sha256').update(normalizeEmail(email)).digest('hex').slice(0, 24)}`;
}

function publicUser(user) {
  return { id: user.id, email: user.email, created_at: user.created_at };
}

function tokenTtlMs() {
  const seconds = Number(process.env.MOCK_TOKEN_TTL_SECONDS || 60 * 60 * 24 * 30);
  return Math.max(60, Number.isFinite(seconds) ? seconds : 60 * 60 * 24 * 30) * 1000;
}

/** Issue an opaque, cryptographically random mock session token. */
export function issueToken(userId) {
  const token = `mock_${crypto.randomBytes(32).toString('base64url')}`;
  tokens.set(token, { userId, expiresAt: Date.now() + tokenTtlMs() });
  return token;
}

/** Revoke a mock session token. */
export function revokeToken(token) {
  if (token) tokens.delete(token);
}

/** Resolve a mock token to a user. Expired/unknown tokens return null. */
export function getUserFromToken(token) {
  if (typeof token !== 'string' || !token) return null;
  const session = tokens.get(token);
  if (!session) return null;
  if (session.expiresAt <= Date.now()) {
    tokens.delete(token);
    return null;
  }
  return usersById.get(session.userId) || null;
}

export function signup(email, password) {
  const normalized = normalizeEmail(email);
  if (!normalized || !password) {
    throw new AppError(400, 'invalid_input', 'email and password required');
  }
  if (usersByEmail.has(normalized)) {
    throw new AppError(409, 'user_exists', 'user already exists');
  }
  const user = {
    id: userIdForEmail(normalized),
    email: normalized,
    password,
    created_at: nowIso()
  };
  usersByEmail.set(normalized, user);
  usersById.set(user.id, user);
  getOrCreateProfile(user.id, normalized);
  return publicUser(user);
}

export function login(email, password) {
  const normalized = normalizeEmail(email);
  const user = usersByEmail.get(normalized);
  if (!user || user.password !== password) {
    throw new AppError(401, 'invalid_credentials', 'invalid credentials');
  }
  getOrCreateProfile(user.id, user.email);
  return publicUser(user);
}

export function findUserByEmail(email) {
  return usersByEmail.get(normalizeEmail(email)) || null;
}

export function getUserById(id) {
  return usersById.get(id) || null;
}

export function getOrCreateProfile(userId, email = null) {
  let profile = profiles.get(userId);
  if (profile) return profile;

  const normalizedEmail = email ? normalizeEmail(email) : null;
  const startingCredits = Math.max(0, Number(process.env.MOCK_STARTING_CREDITS || 0) || 0);
  profile = {
    id: userId,
    email: normalizedEmail,
    display_name: normalizedEmail ? normalizedEmail.split('@')[0] : null,
    tier: 'free',
    credits_balance: 0,
    credits_lifetime_granted: 0,
    credits_lifetime_used: 0,
    credit_period_key: null,
    last_credit_at: null,
    whop_user_id: null,
    whop_membership_id: null,
    subscription_status: null,
    current_period_end: null,
    created_at: nowIso(),
    updated_at: nowIso(),
    mock: true
  };
  profiles.set(userId, profile);

  if (startingCredits > 0) {
    grantCredits(userId, startingCredits, 'mock_starting_credits', { source: 'mock' });
  }
  return profile;
}

export function getProfile(userId) {
  return profiles.get(userId) || null;
}

export function updateProfile(userId, patch = {}) {
  const profile = getOrCreateProfile(userId);
  const allowed = ['display_name', 'email', 'tier', 'subscription_status', 'whop_user_id',
    'whop_membership_id', 'current_period_end', 'credit_period_key', 'last_credit_at'];
  for (const key of allowed) {
    if (Object.prototype.hasOwnProperty.call(patch, key)) profile[key] = patch[key];
  }
  profile.updated_at = nowIso();
  return profile;
}

export function getBalance(userId) {
  return Number(getOrCreateProfile(userId).credits_balance) || 0;
}

export function grantCredits(userId, amount, reason = 'grant', metadata = {}) {
  const value = Number(amount);
  if (!Number.isFinite(value) || value <= 0) {
    throw new AppError(400, 'invalid_amount', 'amount must be positive');
  }
  const profile = getOrCreateProfile(userId);
  profile.credits_balance = roundCredits(Number(profile.credits_balance) + value);
  profile.credits_lifetime_granted = roundCredits(Number(profile.credits_lifetime_granted) + value);
  profile.last_credit_at = nowIso();
  profile.updated_at = nowIso();

  const entry = {
    id: ledgerSeq++,
    user_id: userId,
    delta: value,
    kind: 'grant',
    reason,
    balance_after: profile.credits_balance,
    model: null,
    prompt_tokens: null,
    completion_tokens: null,
    cost_usd: null,
    metadata: metadata || {},
    created_at: nowIso()
  };
  ledger.push(entry);
  return { balance: profile.credits_balance, entry };
}

export function deductCredits(userId, amount, reason = 'deduction', metadata = {}) {
  const value = Number(amount);
  if (!Number.isFinite(value) || value < 0) {
    throw new AppError(400, 'invalid_amount', 'amount must not be negative');
  }
  const profile = getOrCreateProfile(userId);
  if (value === 0) return { balance: Number(profile.credits_balance) || 0, entry: null };

  if (Number(profile.credits_balance) < value) {
    throw insufficientCredits(Number(profile.credits_balance), value);
  }

  profile.credits_balance = roundCredits(Number(profile.credits_balance) - value);
  profile.credits_lifetime_used = roundCredits(Number(profile.credits_lifetime_used) + value);
  profile.updated_at = nowIso();

  const entry = {
    id: ledgerSeq++,
    user_id: userId,
    delta: -value,
    kind: 'deduction',
    reason,
    balance_after: profile.credits_balance,
    model: metadata.model || null,
    prompt_tokens: metadata.prompt_tokens ?? null,
    completion_tokens: metadata.completion_tokens ?? null,
    cost_usd: metadata.cost_usd ?? null,
    metadata: metadata || {},
    created_at: nowIso()
  };
  ledger.push(entry);
  return { balance: profile.credits_balance, entry };
}

/**
 * Mirror of the `record_usage` SQL function: charge credits and store a usage
 * event atomically (single-threaded in the mock store).
 */
export function recordUsage({
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
  const charged = roundCredits(Math.max(0, Number(credits) || 0));
  const balance = deductCredits(userId, charged, 'cloud_ai_usage', {
    model,
    prompt_tokens: promptTokens,
    completion_tokens: completionTokens,
    cost_usd: costUsd,
    request_id: requestId,
    ...metadata
  }).balance;

  const event = {
    id: usageSeq++,
    user_id: userId,
    model,
    provider,
    prompt_tokens: Number(promptTokens) || 0,
    completion_tokens: Number(completionTokens) || 0,
    total_tokens: (Number(promptTokens) || 0) + (Number(completionTokens) || 0),
    cost_usd: Number(costUsd) || 0,
    credits_charged: charged,
    request_id: requestId,
    status,
    metadata: metadata || {},
    created_at: nowIso()
  };
  usage.push(event);
  return { balance, charged, event };
}

export function listLedger(userId, limit = 50) {
  return ledger
    .filter((entry) => entry.user_id === userId)
    .slice(-Math.max(1, Math.min(Number(limit) || 50, 200)))
    .reverse();
}

export function listUsage(userId, limit = 50) {
  return usage
    .filter((entry) => entry.user_id === userId)
    .slice(-Math.max(1, Math.min(Number(limit) || 50, 200)))
    .reverse();
}

export function usageTotals(userId) {
  return usage
    .filter((entry) => entry.user_id === userId)
    .reduce((acc, entry) => {
      acc.requests += 1;
      acc.prompt_tokens += entry.prompt_tokens;
      acc.completion_tokens += entry.completion_tokens;
      acc.cost_usd = roundUsd(acc.cost_usd + entry.cost_usd);
      acc.credits_charged = roundCredits(acc.credits_charged + entry.credits_charged);
      return acc;
    }, { requests: 0, prompt_tokens: 0, completion_tokens: 0, cost_usd: 0, credits_charged: 0 });
}

export function setTier(userId, tier, patch = {}) {
  const normalized = normalizeTier(tier);
  if (!normalized) throw new AppError(400, 'invalid_tier', `unknown tier: ${tier}`);
  return updateProfile(userId, { tier: normalized, ...patch });
}

export function upsertSubscription(subscription) {
  const id = subscription.id || `mock-sub-${subscriptions.size + 1}`;
  const existing = subscriptions.get(id) || {};
  const next = { ...existing, ...subscription, id, updated_at: nowIso(), created_at: existing.created_at || nowIso() };
  subscriptions.set(id, next);
  return next;
}

export function getSubscription(id) {
  return subscriptions.get(id) || null;
}

/** Returns true when this is the first time we see the event id. */
export function rememberWebhook(eventId) {
  if (!eventId) return true;
  if (webhookEvents.has(eventId)) return false;
  webhookEvents.add(eventId);
  return true;
}

/** Remove an idempotency record after a failed handler so a retry can run. */
export function forgetWebhook(eventId) {
  if (eventId) webhookEvents.delete(eventId);
}

export function entitlementsForTier(tier) {
  return featuresForTier(tier).map((feature) => ({
    user_id: null,
    feature,
    enabled: true,
    source: 'plan',
    expires_at: null
  }));
}

export function planForTier(tier) {
  return getPlan(tier);
}

export function creditValue(amount) {
  return roundCredits(Number(amount) * CREDITS_PER_USD);
}

export function resetMockState() {
  usersByEmail.clear();
  usersById.clear();
  profiles.clear();
  ledger.length = 0;
  usage.length = 0;
  subscriptions.clear();
  webhookEvents.clear();
  tokens.clear();
  ledgerSeq = 1;
  usageSeq = 1;
}

function roundCredits(value) {
  return Math.round((Number(value) || 0) * 1e6) / 1e6;
}

function roundUsd(value) {
  return Math.round((Number(value) || 0) * 1e8) / 1e8;
}
