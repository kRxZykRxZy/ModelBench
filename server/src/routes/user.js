import express from 'express';
import { supabaseAdmin, isConfigured } from '../lib/supabase.js';
import * as mock from '../lib/mock.js';
import {
  ensureProfile,
  getCreditSummary,
  getEntitlements,
  listCreditHistory,
  listUsage,
  getUsageTotals
} from '../lib/credits.js';
import { featuresForTier, getPlan, normalizeTier } from '../config/pricing.js';
import { asyncHandler, requireUser } from '../lib/auth.js';
import { AppError } from '../lib/errors.js';

export const userRouter = express.Router();

// GET /user/profile
userRouter.get('/profile', asyncHandler(async (req, res) => {
  const user = await requireUser(req);
  const profile = await ensureProfile(user);
  res.json({ profile });
}));

// PUT /user/profile { display_name }
userRouter.put('/profile', asyncHandler(async (req, res) => {
  const user = await requireUser(req);
  const { display_name } = req.body || {};

  if (display_name !== undefined && (typeof display_name !== 'string' || display_name.length > 80)) {
    throw new AppError(400, 'invalid_display_name', 'display_name must be a string of at most 80 characters');
  }

  const value = typeof display_name === 'string' ? display_name.trim() : null;

  if (!isConfigured()) {
    const profile = mock.updateProfile(user.id, { display_name: value });
    return res.json({ profile });
  }

  const { data, error } = await supabaseAdmin
    .from('profiles')
    .update({ display_name: value, updated_at: new Date().toISOString() })
    .eq('id', user.id)
    .select('*')
    .single();
  if (error) throw new AppError(500, 'profile_error', error.message);
  res.json({ profile: data });
}));

// GET /user/entitlements
userRouter.get('/entitlements', asyncHandler(async (req, res) => {
  const user = await requireUser(req);
  const profile = await ensureProfile(user);
  const tier = normalizeTier(profile?.tier) || 'free';
  const entitlements = await getEntitlements(user.id);
  const features = entitlements.filter((entry) => entry.enabled).map((entry) => entry.feature);
  const plan = getPlan(tier);

  res.json({
    tier,
    features: features.length ? features : featuresForTier(tier),
    entitlements,
    plan: plan ? { tier: plan.tier, name: plan.name, priceUsd: plan.priceUsd, credits: plan.credits } : null,
    credits: {
      balance: Number(profile?.credits_balance || 0),
      lifetime_granted: Number(profile?.credits_lifetime_granted || 0),
      lifetime_used: Number(profile?.credits_lifetime_used || 0)
    }
  });
}));

// GET /user/credits — balance + recent activity
userRouter.get('/credits', asyncHandler(async (req, res) => {
  const user = await requireUser(req);
  await ensureProfile(user);
  const limit = Math.min(Number(req.query.history) || 10, 50);
  const summary = await getCreditSummary(user.id, { historyLimit: limit });
  res.json(summary);
}));

// GET /user/credits/history?limit=50
userRouter.get('/credits/history', asyncHandler(async (req, res) => {
  const user = await requireUser(req);
  const limit = Math.min(Number(req.query.limit) || 50, 200);
  const ledger = await listCreditHistory(user.id, limit);
  res.json({ ledger, balance: (await getCreditSummary(user.id, { historyLimit: 0 })).balance });
}));

// GET /user/usage?limit=50 — cloud AI usage events
userRouter.get('/usage', asyncHandler(async (req, res) => {
  const user = await requireUser(req);
  const limit = Math.min(Number(req.query.limit) || 50, 200);
  const [usage, totals, summary] = await Promise.all([
    listUsage(user.id, limit),
    getUsageTotals(user.id),
    getCreditSummary(user.id, { historyLimit: 0 })
  ]);
  res.json({ usage, totals, balance: summary.balance, tier: summary.tier });
}));
