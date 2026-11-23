import express from 'express';
import { supabase, supabaseAdmin } from '../lib/supabase.js';

export const userRouter = express.Router();

function getToken(req) {
  return (req.headers.authorization || '').replace(/^Bearer\s+/i, '');
}

// GET /user/profile
userRouter.get('/profile', async (req, res) => {
  const token = getToken(req);
  const { data: userData, error: uErr } = await supabase.auth.getUser(token);
  if (uErr) return res.status(401).json({ error: uErr.message });
  const { data, error } = await supabaseAdmin
    .from('profiles').select('*').eq('id', userData.user.id).maybeSingle();
  if (error) return res.status(500).json({ error: error.message });
  res.json({ profile: data || { id: userData.user.id, email: userData.user.email, tier: 'free' } });
});

// PUT /user/profile { display_name }
userRouter.put('/profile', async (req, res) => {
  const token = getToken(req);
  const { data: userData, error: uErr } = await supabase.auth.getUser(token);
  if (uErr) return res.status(401).json({ error: uErr.message });
  const { display_name } = req.body || {};
  const { data, error } = await supabaseAdmin
    .from('profiles').upsert({ id: userData.user.id, display_name, updated_at: new Date().toISOString() })
    .select().single();
  if (error) return res.status(500).json({ error: error.message });
  res.json({ profile: data });
});

// GET /user/entitlements
userRouter.get('/entitlements', async (req, res) => {
  const token = getToken(req);
  const { data: userData, error: uErr } = await supabase.auth.getUser(token);
  if (uErr) return res.status(401).json({ error: uErr.message });
  const { data } = await supabaseAdmin
    .from('profiles').select('tier').eq('id', userData.user.id).maybeSingle();
  const tier = (data && data.tier) || 'free';
  res.json({ tier, features: featuresForTier(tier) });
});

function featuresForTier(tier) {
  const base = ['inference', 'chat', 'models', 'mcp', 'skills'];
  if (tier === 'free') return base;
  return [...base, 'cloud_sync', 'priority_support', 'advanced'];
}
