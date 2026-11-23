import express from 'express';
import { supabase, supabaseAdmin } from '../lib/supabase.js';

export const authRouter = express.Router();

// POST /auth/signup { email, password }
authRouter.post('/signup', async (req, res) => {
  const { email, password } = req.body || {};
  if (!email || !password) return res.status(400).json({ error: 'email and password required' });
  const { data, error } = await supabase.auth.signUp({ email, password });
  if (error) return res.status(400).json({ error: error.message });
  res.json({ user: data.user, session: data.session });
});

// POST /auth/login { email, password }
authRouter.post('/login', async (req, res) => {
  const { email, password } = req.body || {};
  if (!email || !password) return res.status(400).json({ error: 'email and password required' });
  const { data, error } = await supabase.auth.signInWithPassword({ email, password });
  if (error) return res.status(401).json({ error: error.message });
  res.json({ user: data.user, session: data.session });
});

// POST /auth/logout (client-side; just acknowledge)
authRouter.post('/logout', (req, res) => res.json({ ok: true }));

// GET /auth/me  (Authorization: Bearer <access_token>)
authRouter.get('/me', async (req, res) => {
  const token = (req.headers.authorization || '').replace(/^Bearer\s+/i, '');
  if (!token) return res.status(401).json({ error: 'missing token' });
  const { data, error } = await supabase.auth.getUser(token);
  if (error) return res.status(401).json({ error: error.message });
  res.json({ user: data.user });
});
