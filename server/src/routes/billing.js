import express from 'express';
import crypto from 'crypto';
import { whopRequest, planIdForTier, tierForPlanId, verifyWebhookSignature, isConfigured as whopConfigured } from '../lib/whop.js';
import { supabaseAdmin, isConfigured } from '../lib/supabase.js';
import * as mock from '../lib/mock.js';
import { activatePlan, handleWhopEvent } from '../lib/entitlements.js';
import { getProfile } from '../lib/credits.js';
import { plansList, getPlan, normalizeTier } from '../config/pricing.js';
import { asyncHandler, bearerToken, requireUser } from '../lib/auth.js';
import { rateLimit } from '../lib/rateLimit.js';
import { AppError } from '../lib/errors.js';

export const billingRouter = express.Router();

const checkoutLimiter = rateLimit({
  scope: 'billing:checkout',
  windowMs: 60_000,
  max: Number(process.env.CHECKOUT_RATE_LIMIT_MAX || 20)
});

const webhookLimiter = rateLimit({
  scope: 'billing:webhook',
  windowMs: 60_000,
  max: Number(process.env.WEBHOOK_RATE_LIMIT_MAX || 120),
  message: 'Webhook rate limit exceeded'
});

function siteUrl() {
  return (process.env.SITE_URL || `http://localhost:${process.env.PORT || 3000}`).replace(/\/$/, '');
}

// GET /billing/plans — public pricing catalogue
billingRouter.get('/plans', (req, res) => {
  res.json({ plans: plansList(), creditsPerUsd: 1 });
});

// POST /billing/checkout { planId | tier, userId?, successUrl?, cancelUrl? }
billingRouter.post('/checkout', checkoutLimiter, asyncHandler(async (req, res) => {
  const body = req.body || {};
  const requested = body.tier || body.planId || body.plan;
  const tier = normalizeTier(requested) || tierForPlanId(requested);

  if (!tier) throw new AppError(400, 'unknown_plan', 'Unknown plan. Use free, hobby, scale or teams.');
  if (tier === 'free') throw new AppError(400, 'free_plan', 'The free plan does not require checkout.');

  const user = await resolveCheckoutUser(req, body.userId);
  const plan = getPlan(tier);

  // Local development: grant entitlements immediately and return a fake URL so
  // the desktop app's checkout flow can be exercised end to end.
  if (!isConfigured() || !whopConfigured()) {
    if (isConfigured() && !whopConfigured()) {
      throw new AppError(503, 'whop_not_configured', 'Whop is not configured');
    }
    const result = await activatePlan({
      userId: user.id,
      tier,
      membershipId: `mock_${user.id}_${tier}`,
      planId: requested || `plan_${tier}`,
      periodEnd: new Date(Date.now() + 30 * 24 * 60 * 60 * 1000).toISOString(),
      status: 'active',
      // Unique key per mock checkout so local devs can top up repeatedly.
      periodKey: `mock-${new Date().toISOString().slice(0, 7)}-${Date.now()}`,
      eventType: 'mock_checkout',
      metadata: { source: 'mock' }
    });

    return res.json({
      mock: true,
      checkoutUrl: `${siteUrl()}/billing/mock-success?tier=${encodeURIComponent(tier)}`,
      tier,
      plan,
      credits_granted: result.granted,
      balance: result.balance,
      message: 'Mock checkout: credits granted instantly.'
    });
  }

  const whopPlan = planIdForTier(tier);
  if (!whopPlan) {
    throw new AppError(503, 'plan_not_configured', `No Whop plan id configured for ${tier}`);
  }

  const session = await whopRequest('/checkout_sessions', {
    method: 'POST',
    body: JSON.stringify({
      plan_id: whopPlan,
      metadata: { user_id: user.id, tier },
      redirect_url: safeRedirect(body.successUrl) || `${siteUrl()}/billing/success`
    })
  });

  res.json({
    checkoutUrl: session.purchase_url || session.url || '',
    sessionId: session.id || null,
    tier,
    plan
  });
}));

// GET /billing/entitlements?userId=... — lightweight lookup used by the client
billingRouter.get('/entitlements', asyncHandler(async (req, res) => {
  const token = bearerToken(req);
  let userId = req.query.userId;

  if (token) {
    const user = await requireUser(req);
    userId = user.id;
  }
  if (!userId) throw new AppError(400, 'userId_required', 'userId query parameter is required');

  let profile = null;
  if (isConfigured()) {
    profile = await getProfile(userId);
  } else {
    profile = mock.getProfile(userId);
  }

  const tier = normalizeTier(profile?.tier) || 'free';
  const plan = getPlan(tier);
  res.json({
    tier,
    features: plan ? plan.features : getPlan('free').features,
    credits: {
      balance: Number(profile?.credits_balance || 0),
      lifetime_granted: Number(profile?.credits_lifetime_granted || 0),
      lifetime_used: Number(profile?.credits_lifetime_used || 0)
    },
    subscription: {
      status: profile?.subscription_status || 'inactive',
      current_period_end: profile?.current_period_end || null
    }
  });
}));

// GET /billing/mock-success — friendly landing page for mock checkout
billingRouter.get('/mock-success', (req, res) => {
  if (isConfigured()) {
    return res.status(404).json({ error: 'not_found' });
  }
  const tier = String(req.query.tier || 'plan');
  res.type('html').send(`<!doctype html>
<html><head><meta charset="utf-8"><title>ModelBench mock checkout</title>
<style>body{font-family:system-ui;background:#111;color:#eee;display:grid;place-items:center;height:100vh;margin:0}
.card{background:#1b1b1f;padding:32px 40px;border-radius:12px;border:1px solid #333}
code{color:#8bd5a0}</style></head>
<body><div class="card"><h1>Mock checkout complete</h1>
<p>Tier <code>${escapeHtml(tier)}</code> is active and credits were granted.</p>
<p>You can close this tab and return to ModelBench.</p></div></body></html>`);
});

// POST /billing/webhook — Whop lifecycle events (raw body, HMAC verified)
billingRouter.post('/webhook', webhookLimiter, asyncHandler(async (req, res) => {
  const raw = req.body;
  if (!Buffer.isBuffer(raw)) {
    throw new AppError(400, 'raw_body_required', 'Webhook body must be raw JSON (Content-Type: application/json)');
  }

  const verification = verifyWebhookSignature(raw, req.headers);
  if (!verification.ok) {
    const unsignedAllowed = (!isConfigured() && verification.reason === 'webhook_secret_not_configured')
      || String(process.env.ALLOW_UNSIGNED_WEBHOOKS || '').toLowerCase() === 'true';

    if (!unsignedAllowed) {
      throw new AppError(
        401,
        'invalid_webhook_signature',
        `Webhook signature verification failed: ${verification.reason}`
      );
    }
  }

  let event;
  try {
    event = JSON.parse(raw.toString('utf8'));
  } catch {
    throw new AppError(400, 'invalid_json', 'Webhook payload is not valid JSON');
  }

  const eventId = String(
    req.headers['webhook-id']
    || event.id
    || event.event_id
    || crypto.createHash('sha256').update(raw).digest('hex')
  );
  const type = String(event.type || event.action || event.event || '');

  // Idempotency: Whop retries aggressively. A duplicate id is acknowledged.
  if (!isConfigured()) {
    if (!mock.rememberWebhook(eventId)) {
      return res.json({ ok: true, duplicate: true, event_id: eventId });
    }
  } else {
    const { error } = await supabaseAdmin
      .from('webhook_events')
      .insert({ id: eventId, type, payload: event });
    if (error) {
      if (error.code === '23505') {
        return res.json({ ok: true, duplicate: true, event_id: eventId });
      }
      throw new AppError(500, 'webhook_store_error', error.message);
    }
  }

  try {
    const result = await handleWhopEvent(event);
    res.json({
      ok: true,
      verified: verification.ok,
      signature_scheme: verification.scheme || null,
      event_id: eventId,
      ...result
    });
  } catch (error) {
    // Allow Whop to retry: remove the idempotency record if handling failed.
    if (isConfigured()) {
      try {
        await supabaseAdmin.from('webhook_events').delete().eq('id', eventId);
      } catch {
        // best effort
      }
    } else {
      mock.forgetWebhook(eventId);
    }
    throw error;
  }
}));

async function resolveCheckoutUser(req, userId) {
  const token = bearerToken(req);
  if (token) return requireUser(req);

  if (!userId) {
    throw new AppError(401, 'authentication_required', 'Sign in or provide userId');
  }

  if (!isConfigured()) {
    const raw = String(userId);
    const user = raw.includes('@') ? mock.findUserByEmail(raw) : mock.getUserById(raw);
    if (!user) throw new AppError(404, 'user_not_found', 'Mock user not found');
    return { id: user.id, email: user.email };
  }

  if (!/^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i.test(String(userId))) {
    throw new AppError(400, 'invalid_user_id', 'userId must be a Supabase user UUID');
  }
  const profile = await getProfile(String(userId));
  if (!profile) throw new AppError(404, 'user_not_found', 'User profile not found');
  return { id: String(userId) };
}

function safeRedirect(value) {
  if (!value || typeof value !== 'string') return null;
  try {
    const url = new URL(value);
    if (!/^https?:$/.test(url.protocol)) return null;
    return url.toString();
  } catch {
    return null;
  }
}

function escapeHtml(value) {
  return String(value).replace(/[&<>"']/g, (char) => ({
    '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'
  }[char]));
}
