import express from 'express';
import { whopRequest, planIdForTier, isConfigured as whopConfigured } from '../lib/whop.js';
import { supabaseAdmin } from '../lib/supabase.js';
import crypto from 'crypto';

export const billingRouter = express.Router();

// POST /billing/checkout { planId, userId }  (userId = supabase user id)
billingRouter.post('/checkout', async (req, res) => {
  if (!whopConfigured()) return res.status(503).json({ error: 'Whop not configured' });
  const { planId, userId } = req.body || {};
  const tier = (planId || '').replace(/^plan_/, '') || 'pro';
  const whopPlan = planIdForTier(tier) || planId;
  if (!whopPlan) return res.status(400).json({ error: 'unknown plan' });
  try {
    const session = await whopRequest('/checkout_sessions', {
      method: 'POST',
      body: JSON.stringify({ plan_id: whopPlan, metadata: { user_id: userId } })
    });
    res.json({ checkoutUrl: session.purchase_url || session.url || '' });
  } catch (e) {
    res.status(500).json({ error: e.message });
  }
});

// GET /billing/entitlements?userId=...
billingRouter.get('/entitlements', async (req, res) => {
  const userId = req.query.userId;
  if (!userId) return res.status(400).json({ error: 'userId required' });
  const { data } = await supabaseAdmin.from('profiles').select('tier').eq('id', userId).maybeSingle();
  const tier = (data && data.tier) || 'free';
  res.json({ tier, features: tier === 'free' ? ['inference','chat','models'] : ['inference','chat','models','cloud_sync','advanced'] });
});

// POST /billing/webhook  (Whop webhook, raw body)
billingRouter.post('/webhook', async (req, res) => {
  const secret = process.env.WHOP_WEBHOOK_SECRET;
  const sig = req.headers['x-whop-signature'] || req.headers['whop-signature'];
  const raw = req.body;
  // Verify HMAC if secret present
  if (secret && sig) {
    const hmac = crypto.createHmac('sha256', secret).update(raw).digest('hex');
    if (hmac !== sig) return res.status(401).json({ error: 'invalid signature' });
  }
  let event;
  try { event = JSON.parse(raw.toString()); } catch { return res.status(400).json({ error: 'bad json' }); }
  const type = event.type || event.event;
  const userId = (event.data && event.data.metadata && event.data.metadata.user_id) || event.user_id;
  if (type && type.includes('payment') && userId) {
    const tier = (event.data && event.data.plan && event.data.plan.name) || 'pro';
    await supabaseAdmin.from('profiles').upsert({ id: userId, tier: tier.toLowerCase(), updated_at: new Date().toISOString() });
  }
  if (type && type.includes('cancel') && userId) {
    await supabaseAdmin.from('profiles').upsert({ id: userId, tier: 'free', updated_at: new Date().toISOString() });
  }
  res.json({ ok: true });
});
