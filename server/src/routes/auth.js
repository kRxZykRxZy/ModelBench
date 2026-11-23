import express from 'express';
import { supabase, isConfigured } from '../lib/supabase.js';
import * as mock from '../lib/mock.js';
import { ensureProfile } from '../lib/credits.js';
import { asyncHandler, bearerToken, requireUser } from '../lib/auth.js';
import { rateLimit } from '../lib/rateLimit.js';
import { AppError } from '../lib/errors.js';

export const authRouter = express.Router();

const signupLimiter = rateLimit({
  scope: 'auth:signup',
  windowMs: 15 * 60 * 1000,
  max: Number(process.env.AUTH_RATE_LIMIT_MAX || 10),
  message: 'Too many signup attempts. Try again later.'
});

const loginLimiter = rateLimit({
  scope: 'auth:login',
  windowMs: 15 * 60 * 1000,
  max: Number(process.env.AUTH_RATE_LIMIT_MAX || 10),
  message: 'Too many login attempts. Try again later.'
});

function validateCredentials(email, password, { requirePassword = true } = {}) {
  if (!email || typeof email !== 'string' || !/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email.trim())) {
    throw new AppError(400, 'invalid_email', 'A valid email is required');
  }
  if (requirePassword && (!password || typeof password !== 'string')) {
    throw new AppError(400, 'invalid_password', 'Password is required');
  }
}

// POST /auth/signup { email, password }
authRouter.post('/signup', signupLimiter, asyncHandler(async (req, res) => {
  const { email, password } = req.body || {};
  validateCredentials(email, password);

  const minLength = Number(process.env.PASSWORD_MIN_LENGTH || 8);
  if (password.length < minLength) {
    throw new AppError(400, 'weak_password', `Password must be at least ${minLength} characters`);
  }

  if (!isConfigured()) {
    const user = mock.signup(email, password);
    const token = mock.issueToken(user.id);
    return res.status(201).json({
      user,
      session: { access_token: token, token_type: 'bearer', user }
    });
  }

  const { data, error } = await supabase.auth.signUp({ email: email.trim(), password });
  if (error) throw new AppError(400, 'signup_failed', error.message);

  if (data.user) {
    // Safety net when the SQL trigger is not installed yet.
    await ensureProfile(data.user).catch(() => {});
  }

  res.status(201).json({ user: data.user, session: data.session });
}));

// POST /auth/login { email, password }
authRouter.post('/login', loginLimiter, asyncHandler(async (req, res) => {
  const { email, password } = req.body || {};
  validateCredentials(email, password);

  if (!isConfigured()) {
    const user = mock.login(email, password);
    const token = mock.issueToken(user.id);
    return res.json({
      user,
      session: { access_token: token, token_type: 'bearer', user }
    });
  }

  const { data, error } = await supabase.auth.signInWithPassword({ email: email.trim(), password });
  if (error) throw new AppError(401, 'invalid_credentials', error.message);

  if (data.user) {
    await ensureProfile(data.user).catch(() => {});
  }

  res.json({ user: data.user, session: data.session });
}));

// POST /auth/logout — revokes the mock session; Supabase tokens are client-side.
authRouter.post('/logout', asyncHandler(async (req, res) => {
  if (!isConfigured()) {
    mock.revokeToken(bearerToken(req));
  }
  res.json({ ok: true });
}));

// GET /auth/me (Authorization: Bearer <access_token>)
authRouter.get('/me', asyncHandler(async (req, res) => {
  const user = await requireUser(req);
  res.json({ user });
}));
