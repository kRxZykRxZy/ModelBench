import '../env.js';
import { createClient } from '@supabase/supabase-js';

const url = process.env.SUPABASE_URL;
const anon = process.env.SUPABASE_ANON_KEY;
const service = process.env.SUPABASE_SERVICE_ROLE_KEY;

const configured = Boolean(url && anon && service);

/**
 * Supabase clients are only constructed when all three values are present.
 * In local development the server runs in mock mode with in-memory storage.
 */
export const supabase = configured
  ? createClient(url, anon, {
      auth: { autoRefreshToken: false, persistSession: false }
    })
  : null;

export const supabaseAdmin = configured
  ? createClient(url, service, {
      auth: { autoRefreshToken: false, persistSession: false }
    })
  : null;

export function isConfigured() {
  return configured;
}

export function isMockMode() {
  return !configured;
}
