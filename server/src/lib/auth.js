import { supabase, isConfigured } from './supabase.js';
import { getUserFromToken } from './mock.js';
import { AppError } from './errors.js';

export function bearerToken(req) {
  const header = req.headers?.authorization || '';
  const match = /^Bearer\s+(.+)$/i.exec(header.trim());
  return match ? match[1].trim() : '';
}

/**
 * Resolve the authenticated user for a request.
 *
 * Mock mode: opaque token issued by /auth/signup or /auth/login.
 * Live mode: Supabase access token verified against the auth server.
 */
export async function requireUser(req) {
  const token = bearerToken(req);
  if (!token) {
    throw new AppError(401, 'missing_token', 'Authorization: Bearer <token> header required');
  }

  if (!isConfigured()) {
    const user = getUserFromToken(token);
    if (!user) {
      throw new AppError(401, 'invalid_token', 'Invalid or expired mock token');
    }
    return { id: user.id, email: user.email, mock: true };
  }

  const { data, error } = await supabase.auth.getUser(token);
  if (error || !data?.user) {
    throw new AppError(401, 'invalid_token', error?.message || 'Invalid access token');
  }
  return data.user;
}

/** Wrap async route handlers so rejected promises reach the error middleware. */
export function asyncHandler(fn) {
  return function wrapped(req, res, next) {
    Promise.resolve(fn(req, res, next)).catch(next);
  };
}
