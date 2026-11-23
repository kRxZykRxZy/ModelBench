/**
 * End-to-end smoke test for the ModelBench backend.
 *
 * Usage:
 *   node scripts/smoke.mjs                 # against http://localhost:3000
 *   BASE_URL=http://localhost:3000 node scripts/smoke.mjs
 *
 * The server must be running in mock mode (no Supabase/OpenRouter keys) for the
 * credit and AI assertions to be deterministic.
 */
const BASE_URL = (process.env.BASE_URL || 'http://localhost:3000').replace(/\/$/, '');
let failures = 0;

function log(name, ok, detail = '') {
  const mark = ok ? 'PASS' : 'FAIL';
  console.log(`[${mark}] ${name}${detail ? ` — ${detail}` : ''}`);
  if (!ok) failures += 1;
}

async function request(path, { method = 'GET', token, body, headers = {} } = {}) {
  const response = await fetch(BASE_URL + path, {
    method,
    headers: {
      ...(body ? { 'Content-Type': 'application/json' } : {}),
      ...(token ? { Authorization: `Bearer ${token}` } : {}),
      ...headers
    },
    body: body ? JSON.stringify(body) : undefined
  });
  const text = await response.text();
  let json = null;
  try { json = text ? JSON.parse(text) : null; } catch { /* stream or html */ }
  return { status: response.status, json, text, headers: response.headers };
}

const stamp = Date.now();
const email = `smoke-${stamp}@example.com`;

const health = await request('/health');
log('health', health.status === 200 && health.json?.ok === true, health.json?.mode);

const signup = await request('/auth/signup', {
  method: 'POST',
  body: { email, password: 'smoke-password-123' }
});
const token = signup.json?.session?.access_token;
log('signup returns opaque token', signup.status === 201 && Boolean(token) && !token.includes(email));

const credits0 = await request('/user/credits', { token });
log('free balance is 0', credits0.status === 200 && credits0.json?.balance === 0, String(credits0.json?.balance));

const models = await request('/ai/models');
const ids = (models.json?.models || []).map((model) => model.id.toLowerCase()).join(' ');
log('catalogue has models', models.status === 200 && models.json.models.length > 10, String(models.json?.count));
log('catalogue excludes GPT/Claude', !/(gpt|claude|openai|anthropic|gemini)/.test(ids));

const freeChat = await request('/ai/chat', {
  method: 'POST',
  token,
  body: { model: 'deepseek-v4.1-flash-free', messages: [{ role: 'user', content: 'hello' }] }
});
log('free model works with 0 credits', freeChat.status === 200 && freeChat.json?.modelbench?.mock === true);

const paidWithoutCredits = await request('/ai/chat', {
  method: 'POST',
  token,
  body: { model: 'deepseek-v4.1-flash', messages: [{ role: 'user', content: 'hello' }] }
});
log('paid model blocked at 0 credits', paidWithoutCredits.status === 402, paidWithoutCredits.json?.error);

const checkout = await request('/billing/checkout', {
  method: 'POST',
  token,
  body: { planId: 'plan_hobby' }
});
log('mock checkout grants 7 credits', checkout.status === 200 && checkout.json?.credits_granted === 7, String(checkout.json?.credits_granted));

const credits7 = await request('/user/credits', { token });
log('balance is 7', credits7.status === 200 && credits7.json?.balance === 7, String(credits7.json?.balance));

const paidChat = await request('/ai/chat', {
  method: 'POST',
  token,
  body: {
    model: 'deepseek-v4.1-flash',
    max_tokens: 128,
    messages: [{ role: 'user', content: 'Explain credits in one sentence.' }]
  }
});
const charged = paidChat.json?.modelbench?.credits_charged;
log('paid model charges credits', paidChat.status === 200 && charged > 0, String(charged));
log('balance decreased', paidChat.json?.modelbench?.balance < 7, String(paidChat.json?.modelbench?.balance));

const streamResponse = await fetch(BASE_URL + '/ai/chat', {
  method: 'POST',
  headers: { 'Content-Type': 'application/json', Authorization: `Bearer ${token}` },
  body: JSON.stringify({
    model: 'deepseek-v4.1-flash',
    stream: true,
    max_tokens: 64,
    messages: [{ role: 'user', content: 'stream please' }]
  })
});
const streamText = await streamResponse.text();
log('stream returns SSE usage event', streamResponse.status === 200
  && streamText.includes('event: modelbench.usage')
  && streamText.includes('data: [DONE]'));

// Webhook lifecycle on a second user.
const email2 = `smoke-hook-${stamp}@example.com`;
const signup2 = await request('/auth/signup', {
  method: 'POST',
  body: { email: email2, password: 'smoke-password-123' }
});
const token2 = signup2.json?.session?.access_token;
const me2 = await request('/auth/me', { token: token2 });
const userId2 = me2.json?.user?.id;

const hookId = `evt_${stamp}`;
const hook = await request('/billing/webhook', {
  method: 'POST',
  headers: { 'webhook-id': hookId },
  body: {
    action: 'membership_activated',
    data: {
      id: `mem_${stamp}`,
      user_id: userId2,
      plan_id: 'plan_scale',
      status: 'active',
      metadata: { user_id: userId2 },
      current_period_end: Math.floor(Date.now() / 1000) + 30 * 24 * 3600
    }
  }
});
log('webhook activates plan', hook.status === 200 && hook.json?.action === 'activated', hook.json?.reason);

const creditsHook = await request('/user/credits', { token: token2 });
log('webhook granted scale credits', creditsHook.json?.balance === 25, String(creditsHook.json?.balance));

const duplicate = await request('/billing/webhook', {
  method: 'POST',
  headers: { 'webhook-id': hookId },
  body: { action: 'membership_activated', data: { user_id: userId2, plan_id: 'plan_scale' } }
});
log('duplicate webhook is ignored', duplicate.status === 200 && duplicate.json?.duplicate === true);

const cancel = await request('/billing/webhook', {
  method: 'POST',
  headers: { 'webhook-id': `evt_cancel_${stamp}` },
  body: { action: 'membership_deactivated', data: { id: `mem_${stamp}`, user_id: userId2, plan_id: 'plan_scale' } }
});
log('webhook cancels plan', cancel.status === 200 && cancel.json?.action === 'canceled');

const creditsAfterCancel = await request('/user/credits', { token: token2 });
log('credits survive cancellation', creditsAfterCancel.json?.balance === 25 && creditsAfterCancel.json?.tier === 'free');

console.log(failures === 0 ? '\nAll smoke checks passed.' : `\n${failures} smoke check(s) failed.`);
process.exitCode = failures === 0 ? 0 : 1;
