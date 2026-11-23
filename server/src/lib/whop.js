import dotenv from 'dotenv';

dotenv.config();

const apiKey = process.env.WHOP_API_KEY;
const baseUrl = 'https://api.whop.com/api/v2';

export async function whopRequest(path, options = {}) {
  const res = await fetch(baseUrl + path, {
    ...options,
    headers: {
      'Authorization': `Bearer ${apiKey}`,
      'Content-Type': 'application/json',
      ...(options.headers || {})
    }
  });
  if (!res.ok) {
    const text = await res.text();
    throw new Error(`Whop ${res.status}: ${text}`);
  }
  return res.json();
}

export function isConfigured() {
  return Boolean(apiKey);
}

export function planIdForTier(tier) {
  switch (tier) {
    case 'pro': return process.env.WHOP_PLAN_PRO;
    case 'ultimate': return process.env.WHOP_PLAN_ULTIMATE;
    case 'teams': return process.env.WHOP_PLAN_TEAMS;
    default: return null;
  }
}
