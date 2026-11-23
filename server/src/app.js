import './env.js';
import express from 'express';
import cors from 'cors';
import { authRouter } from './routes/auth.js';
import { billingRouter } from './routes/billing.js';
import { userRouter } from './routes/user.js';
import { aiRouter } from './routes/ai.js';
import { isConfigured } from './lib/supabase.js';
import { AppError } from './lib/errors.js';

export function createApp() {
  const app = express();

  app.disable('x-powered-by');
  app.set('trust proxy', Number(process.env.TRUST_PROXY || 0));

  app.use(cors(corsOptions()));

  // Whop webhooks must be verified against the exact raw bytes, so raw parsing
  // happens before the global JSON parser.
  app.use('/billing/webhook', express.raw({
    type: ['application/json', 'application/*+json'],
    limit: '1mb'
  }));
  app.use(express.json({ limit: '2mb' }));

  app.get('/health', (req, res) => {
    res.json({
      ok: true,
      service: 'modelbench-server',
      version: '0.1.0',
      mode: isConfigured() ? 'live' : 'mock',
      ai: process.env.OPENROUTER_API_KEY && process.env.AI_MOCK !== 'true'
        ? 'openrouter'
        : 'mock',
      billing: process.env.WHOP_API_KEY ? 'whop' : 'mock'
    });
  });

  app.use('/auth', authRouter);
  app.use('/user', userRouter);
  app.use('/billing', billingRouter);
  app.use('/ai', aiRouter);

  app.use((req, res) => {
    res.status(404).json({ error: 'not_found', message: `No route for ${req.method} ${req.path}` });
  });

  // eslint-disable-next-line no-unused-vars
  app.use((error, req, res, next) => {
    const status = error instanceof AppError ? error.status : (error.status || 500);

    if (error.type === 'entity.parse.failed') {
      return res.status(400).json({ error: 'invalid_json', message: 'Request body is not valid JSON' });
    }
    if (error.type === 'entity.too.large') {
      return res.status(413).json({ error: 'payload_too_large', message: 'Request body is too large' });
    }

    if (status >= 500) {
      console.error(`[error] ${req.method} ${req.path}:`, error);
    }

    const payload = error instanceof AppError
      ? error.toJSON()
      : { error: 'internal_error', message: status >= 500 ? 'Internal server error' : error.message };

    res.status(status).json(payload);
  });

  return app;
}

function corsOptions() {
  const configured = (process.env.ALLOWED_ORIGINS || process.env.CORS_ORIGINS || '')
    .split(',')
    .map((value) => value.trim())
    .filter(Boolean);

  const defaults = ['http://localhost:3000', 'http://127.0.0.1:3000'];
  if (process.env.SITE_URL) defaults.push(process.env.SITE_URL.replace(/\/$/, ''));

  const allowlist = [...new Set([...defaults, ...configured])];
  const wildcard = allowlist.includes('*');

  return {
    origin(origin, callback) {
      // Native desktop clients and curl send no Origin header.
      if (!origin) return callback(null, true);
      if (wildcard || allowlist.includes(origin)) return callback(null, true);
      return callback(null, false);
    },
    credentials: true,
    methods: ['GET', 'POST', 'PUT', 'PATCH', 'DELETE', 'OPTIONS'],
    allowedHeaders: [
      'Content-Type',
      'Authorization',
      'X-Requested-With',
      'X-Whop-Signature',
      'Whop-Signature',
      'webhook-id',
      'webhook-timestamp',
      'webhook-signature'
    ],
    exposedHeaders: ['RateLimit-Limit', 'RateLimit-Remaining', 'RateLimit-Reset', 'Retry-After'],
    maxAge: 600
  };
}
