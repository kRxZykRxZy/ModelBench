/**
 * Tiny in-memory fixed-window rate limiter.
 *
 * Good enough for a single-node backend (which is what ModelBench ships).
 * For multi-instance deployments, swap the store for Redis or use an edge
 * provider. Keys are scoped per route + client IP so one noisy endpoint cannot
 * exhaust another endpoint's budget.
 */

const buckets = new Map();
let lastSweep = 0;

function disabled() {
  return String(process.env.RATE_LIMIT_DISABLED || '').toLowerCase() === 'true';
}

function clientKey(req, scope) {
  // Rely on Express's req.ip. When TRUST_PROXY is enabled Express derives it
  // from X-Forwarded-For; when disabled, client-supplied headers are ignored so
  // rate limits cannot be bypassed by spoofing X-Forwarded-For.
  const ip = req.ip || req.socket?.remoteAddress || 'unknown';
  return `${scope}:${ip}`;
}

function sweep(now) {
  if (now - lastSweep < 60_000) return;
  lastSweep = now;
  for (const [key, bucket] of buckets) {
    if (bucket.resetAt <= now) buckets.delete(key);
  }
}

/**
 * @param {object} options
 * @param {string} options.scope Unique bucket name, e.g. "auth:login".
 * @param {number} options.windowMs Window length in milliseconds.
 * @param {number} options.max Requests allowed per window.
 * @param {string} [options.message]
 */
export function rateLimit({ scope, windowMs = 60_000, max = 60, message = 'Too many requests' }) {
  return function rateLimiter(req, res, next) {
    if (disabled()) return next();

    const now = Date.now();
    sweep(now);

    const key = clientKey(req, scope);
    let bucket = buckets.get(key);

    if (!bucket || bucket.resetAt <= now) {
      bucket = { count: 0, resetAt: now + windowMs };
      buckets.set(key, bucket);
    }

    bucket.count += 1;

    const remaining = Math.max(0, max - bucket.count);
    res.setHeader('RateLimit-Limit', String(max));
    res.setHeader('RateLimit-Remaining', String(remaining));
    res.setHeader('RateLimit-Reset', String(Math.ceil(bucket.resetAt / 1000)));

    if (bucket.count > max) {
      const retryAfter = Math.max(1, Math.ceil((bucket.resetAt - now) / 1000));
      res.setHeader('Retry-After', String(retryAfter));
      return res.status(429).json({
        error: 'rate_limited',
        message,
        retryAfter
      });
    }

    next();
  };
}

/** Test helper: clear all buckets. */
export function resetRateLimits() {
  buckets.clear();
  lastSweep = 0;
}
