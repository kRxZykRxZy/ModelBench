import '../env.js';
import crypto from 'crypto';
import { AppError } from './errors.js';
import { planIdForTier, tierForPlanId } from '../config/pricing.js';

const baseUrl = process.env.WHOP_API_BASE_URL || 'https://api.whop.com/api/v2';

export async function whopRequest(path, options = {}) {
  const apiKey = process.env.WHOP_API_KEY;
  if (!apiKey) {
    throw new AppError(503, 'whop_not_configured', 'Whop is not configured');
  }

  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), Number(process.env.WHOP_TIMEOUT_MS || 15_000));
  try {
    const res = await fetch(baseUrl + path, {
      ...options,
      signal: controller.signal,
      headers: {
        Authorization: `Bearer ${apiKey}`,
        'Content-Type': 'application/json',
        ...(options.headers || {})
      }
    });
    const text = await res.text();
    if (!res.ok) {
      throw new AppError(502, 'whop_error', `Whop ${res.status}: ${text.slice(0, 500)}`);
    }
    return text ? JSON.parse(text) : {};
  } catch (error) {
    if (error.name === 'AbortError') {
      throw new AppError(504, 'whop_timeout', 'Whop request timed out');
    }
    if (error instanceof AppError) throw error;
    throw new AppError(502, 'whop_error', error.message);
  } finally {
    clearTimeout(timeout);
  }
}

export function isConfigured() {
  return Boolean(process.env.WHOP_API_KEY);
}

export { planIdForTier, tierForPlanId };

/**
 * Verify a Whop webhook signature.
 *
 * Supports both the Standard Webhooks scheme used by current Whop webhooks
 * (`webhook-id`, `webhook-timestamp`, `webhook-signature: v1,<base64>`) and the
 * legacy `X-Whop-Signature` HMAC-SHA256 hex header.
 *
 * @returns {{ok:boolean, scheme?:string, reason?:string}}
 */
export function verifyWebhookSignature(rawBody, headers = {}, secret = process.env.WHOP_WEBHOOK_SECRET) {
  if (!secret) return { ok: false, reason: 'webhook_secret_not_configured' };

  const body = Buffer.isBuffer(rawBody) ? rawBody : Buffer.from(String(rawBody ?? ''), 'utf8');
  if (body.length === 0) return { ok: false, reason: 'empty_body' };

  const get = (name) => {
    const value = headers[name] ?? headers[name.toLowerCase()] ?? headers[name.toUpperCase()];
    return Array.isArray(value) ? value[0] : value;
  };

  // --- Standard Webhooks -----------------------------------------------------
  const webhookId = get('webhook-id');
  const webhookTimestamp = get('webhook-timestamp');
  const webhookSignature = get('webhook-signature');

  if (webhookSignature && webhookId && webhookTimestamp) {
    const timestamp = Number(webhookTimestamp);
    const tolerance = Number(process.env.WHOP_WEBHOOK_TOLERANCE_SECONDS || 300);
    if (!Number.isFinite(timestamp) || Math.abs(Date.now() / 1000 - timestamp) > tolerance) {
      return { ok: false, reason: 'timestamp_out_of_tolerance' };
    }

    const key = decodeStandardSecret(secret);
    const signedPayload = Buffer.from(`${webhookId}.${webhookTimestamp}.${body.toString('utf8')}`, 'utf8');
    const expected = crypto.createHmac('sha256', key).update(signedPayload).digest();

    const candidates = String(webhookSignature)
      .split(' ')
      .flatMap((part) => part.split(','))
      .map((part) => part.trim())
      .filter((part) => part && part !== 'v1')
      .map((part) => safeBase64Decode(part))
      .filter(Boolean);

    if (candidates.some((candidate) => safeEqual(candidate, expected))) {
      return { ok: true, scheme: 'standard-webhooks' };
    }
    return { ok: false, reason: 'invalid_signature' };
  }

  // --- Legacy Whop header ----------------------------------------------------
  const legacy = get('x-whop-signature') || get('whop-signature');
  if (legacy) {
    const key = Buffer.from(secret, 'utf8');
    const digest = crypto.createHmac('sha256', key).update(body).digest();
    const normalized = String(legacy).trim().replace(/^sha256=/i, '');
    const candidates = [
      safeHexDecode(normalized),
      safeBase64Decode(normalized)
    ].filter(Boolean);

    if (candidates.some((candidate) => safeEqual(candidate, digest))) {
      return { ok: true, scheme: 'legacy-hmac' };
    }
    return { ok: false, reason: 'invalid_signature' };
  }

  return { ok: false, reason: 'missing_signature' };
}

/**
 * Some Whop accounts use a `whsec_<base64>` secret (Standard Webhooks format).
 * Accept both raw and prefixed secrets so local testing stays easy.
 */
function decodeStandardSecret(secret) {
  if (String(secret).startsWith('whsec_')) {
    const decoded = safeBase64Decode(String(secret).slice('whsec_'.length));
    if (decoded) return decoded;
  }
  return Buffer.from(String(secret), 'utf8');
}

function safeBase64Decode(value) {
  if (!value) return null;
  try {
    const buffer = Buffer.from(value, 'base64');
    return buffer.length > 0 ? buffer : null;
  } catch {
    return null;
  }
}

function safeHexDecode(value) {
  if (!value || !/^[0-9a-f]+$/i.test(value) || value.length % 2 !== 0) return null;
  try {
    return Buffer.from(value, 'hex');
  } catch {
    return null;
  }
}

function safeEqual(a, b) {
  if (!a || !b || a.length !== b.length) return false;
  try {
    return crypto.timingSafeEqual(a, b);
  } catch {
    return false;
  }
}
