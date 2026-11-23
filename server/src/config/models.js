/**
 * OpenRouter model catalogue for ModelBench cloud AI.
 *
 * Policy: ModelBench only exposes Chinese / open-weight models. OpenAI (GPT),
 * Anthropic (Claude) and Google (Gemini) models are intentionally excluded.
 * A hard assertion at the bottom of this file fails the process if a forbidden
 * model ever sneaks into the catalogue.
 *
 * Prices are normalised to USD per 1,000,000 tokens. OpenRouter publishes
 * per-token strings (e.g. "0.00000006"); `toPublicModel()` exposes both forms
 * so clients can display either. Snapshot: 2026-11-23.
 * Refresh source: https://openrouter.ai/api/v1/models
 */

export const OPENROUTER_BASE_URL = 'https://openrouter.ai/api/v1';
export const PRICING_SNAPSHOT = '2026-11-23';
export const PRICING_SOURCE = 'https://openrouter.ai/api/v1/models';

/** Build a catalogue entry. Pricing is USD per 1M tokens. */
function model({
  id,
  slug,
  aliases = [],
  name,
  provider,
  contextWindow,
  maxOutput = 16384,
  modality = 'text',
  capabilities = ['chat'],
  promptPerMTok = 0,
  completionPerMTok = 0,
  free = false,
  recommended = false
}) {
  return Object.freeze({
    id,
    slug,
    aliases: Object.freeze([...new Set([slug, ...aliases])]),
    name,
    provider,
    contextWindow,
    maxOutput,
    modality,
    capabilities: Object.freeze(capabilities),
    pricing: Object.freeze({ promptPerMTok, completionPerMTok }),
    free: free || (promptPerMTok === 0 && completionPerMTok === 0),
    recommended
  });
}

export const MODELS = Object.freeze([
  // ---------------------------------------------------------------- DeepSeek
  model({
    id: 'deepseek/deepseek-v4.1-flash',
    slug: 'deepseek-v4.1-flash',
    aliases: ['deepseek/deepseek-v4.1-flash-20261101'],
    name: 'DeepSeek V4.1 Flash',
    provider: 'DeepSeek',
    contextWindow: 1_000_000,
    maxOutput: 65536,
    capabilities: ['chat', 'tools', 'reasoning', 'json'],
    promptPerMTok: 0.10,
    completionPerMTok: 0.50,
    recommended: true
  }),
  model({
    id: 'deepseek/deepseek-chat-v3.2',
    slug: 'deepseek-chat-v3.2',
    aliases: ['deepseek-chat', 'deepseek-v3.2'],
    name: 'DeepSeek V3.2',
    provider: 'DeepSeek',
    contextWindow: 163_840,
    maxOutput: 32768,
    capabilities: ['chat', 'tools', 'json'],
    promptPerMTok: 0.28,
    completionPerMTok: 0.42
  }),
  model({
    id: 'deepseek/deepseek-r1-0528',
    slug: 'deepseek-r1',
    aliases: ['deepseek-r1-0528'],
    name: 'DeepSeek R1 (0528)',
    provider: 'DeepSeek',
    contextWindow: 163_840,
    maxOutput: 32768,
    capabilities: ['chat', 'reasoning'],
    promptPerMTok: 0.55,
    completionPerMTok: 2.19
  }),

  // --------------------------------------------------------------- Alibaba Qwen
  model({
    id: 'qwen/qwen3-max',
    slug: 'qwen3-max',
    name: 'Qwen3 Max',
    provider: 'Alibaba',
    contextWindow: 262_144,
    maxOutput: 32768,
    capabilities: ['chat', 'tools', 'reasoning', 'json'],
    promptPerMTok: 1.2,
    completionPerMTok: 6.0
  }),
  model({
    id: 'qwen/qwen3-coder',
    slug: 'qwen3-coder',
    aliases: ['qwen3-coder-480b'],
    name: 'Qwen3 Coder',
    provider: 'Alibaba',
    contextWindow: 262_144,
    maxOutput: 65536,
    capabilities: ['chat', 'tools', 'code'],
    promptPerMTok: 0.3,
    completionPerMTok: 1.2
  }),
  model({
    id: 'qwen/qwen3-235b-a22b-2507',
    slug: 'qwen3-235b-a22b',
    aliases: ['qwen3-235b'],
    name: 'Qwen3 235B A22B',
    provider: 'Alibaba',
    contextWindow: 262_144,
    maxOutput: 32768,
    capabilities: ['chat', 'tools', 'reasoning'],
    promptPerMTok: 0.20,
    completionPerMTok: 0.70
  }),

  // ----------------------------------------------------------------- Moonshot
  model({
    id: 'moonshotai/kimi-k2.5',
    slug: 'kimi-k2.5',
    name: 'Kimi K2.5',
    provider: 'Moonshot AI',
    contextWindow: 262_144,
    maxOutput: 32768,
    capabilities: ['chat', 'tools', 'reasoning', 'json'],
    promptPerMTok: 0.6,
    completionPerMTok: 2.5
  }),
  model({
    id: 'moonshotai/kimi-k2',
    slug: 'kimi-k2',
    name: 'Kimi K2',
    provider: 'Moonshot AI',
    contextWindow: 131_072,
    maxOutput: 16384,
    capabilities: ['chat', 'tools'],
    promptPerMTok: 0.55,
    completionPerMTok: 2.2
  }),

  // ---------------------------------------------------------------- Zhipu GLM
  model({
    id: 'z-ai/glm-4.6',
    slug: 'glm-4.6',
    name: 'GLM-4.6',
    provider: 'Zhipu AI',
    contextWindow: 202_752,
    maxOutput: 32768,
    capabilities: ['chat', 'tools', 'reasoning'],
    promptPerMTok: 0.6,
    completionPerMTok: 2.2
  }),
  model({
    id: 'z-ai/glm-5',
    slug: 'glm-5',
    name: 'GLM-5',
    provider: 'Zhipu AI',
    contextWindow: 202_752,
    maxOutput: 32768,
    capabilities: ['chat', 'tools', 'reasoning'],
    promptPerMTok: 0.8,
    completionPerMTok: 2.6,
    recommended: true
  }),

  // ------------------------------------------------------------------ MiniMax
  model({
    id: 'minimax/minimax-m2',
    slug: 'minimax-m2',
    name: 'MiniMax M2',
    provider: 'MiniMax',
    contextWindow: 204_800,
    maxOutput: 32768,
    capabilities: ['chat', 'tools', 'reasoning'],
    promptPerMTok: 0.3,
    completionPerMTok: 1.2
  }),
  model({
    id: 'minimax/minimax-m2.1',
    slug: 'minimax-m2.1',
    name: 'MiniMax M2.1',
    provider: 'MiniMax',
    contextWindow: 204_800,
    maxOutput: 32768,
    capabilities: ['chat', 'tools', 'reasoning'],
    promptPerMTok: 0.35,
    completionPerMTok: 1.4
  }),

  // -------------------------------------------------------------- Xiaomi MiMo
  model({
    id: 'xiaomi/mimo-v2-flash',
    slug: 'mimo-v2-flash',
    name: 'MiMo V2 Flash',
    provider: 'Xiaomi',
    contextWindow: 131_072,
    maxOutput: 16384,
    capabilities: ['chat', 'tools'],
    promptPerMTok: 0.1,
    completionPerMTok: 0.3
  }),
  model({
    id: 'xiaomi/mimo-v2.6-flash:free',
    slug: 'mimo-v2.6-flash-free',
    aliases: ['mimo-v2.6-flash'],
    name: 'MiMo V2.6 Flash (free)',
    provider: 'Xiaomi',
    contextWindow: 262_144,
    maxOutput: 32768,
    capabilities: ['chat', 'tools', 'reasoning'],
    free: true,
    recommended: true
  }),

  // --------------------------------------------------------------- Tencent Hunyuan
  model({
    id: 'tencent/hunyuan-a13b-instruct',
    slug: 'hunyuan-a13b',
    name: 'Hunyuan A13B Instruct',
    provider: 'Tencent',
    contextWindow: 131_072,
    maxOutput: 16384,
    capabilities: ['chat', 'tools'],
    promptPerMTok: 0.14,
    completionPerMTok: 0.57
  }),

  // ---------------------------------------------------------------- Baidu Ernie
  model({
    id: 'baidu/ernie-4.5-300b-a47b',
    slug: 'ernie-4.5',
    aliases: ['ernie-4.5-300b'],
    name: 'ERNIE 4.5 300B A47B',
    provider: 'Baidu',
    contextWindow: 123_000,
    maxOutput: 16384,
    capabilities: ['chat', 'tools'],
    promptPerMTok: 0.28,
    completionPerMTok: 1.1
  }),

  // ------------------------------------------------------------------ ByteDance
  model({
    id: 'bytedance/seed-oss-36b-instruct',
    slug: 'seed-oss-36b',
    name: 'Seed OSS 36B Instruct',
    provider: 'ByteDance',
    contextWindow: 131_072,
    maxOutput: 32768,
    capabilities: ['chat', 'tools', 'reasoning'],
    promptPerMTok: 0.1,
    completionPerMTok: 0.2
  }),

  // -------------------------------------------------------------------- NVIDIA
  model({
    id: 'nvidia/nemotron-super-vl',
    slug: 'nemotron-super-vl',
    name: 'Nemotron Super VL',
    provider: 'NVIDIA',
    contextWindow: 131_072,
    maxOutput: 16384,
    modality: 'vision',
    capabilities: ['chat', 'vision', 'tools'],
    promptPerMTok: 0.2,
    completionPerMTok: 0.6,
    recommended: true
  }),
]);

const FORBIDDEN = /(?:^|[^a-z])(gpt|chatgpt|claude|openai|anthropic|gemini|google)(?:[^a-z]|$)/i;

for (const entry of MODELS) {
  const haystack = `${entry.id} ${entry.name} ${entry.provider}`;
  if (FORBIDDEN.test(haystack)) {
    throw new Error(`Forbidden model in catalogue: ${entry.id}. GPT/Claude/Gemini models are not allowed.`);
  }
}

/** Every accepted identifier (id, slug, alias) mapped to a model. */
const LOOKUP = new Map();
for (const entry of MODELS) {
  for (const key of [entry.id, entry.slug, ...entry.aliases]) {
    LOOKUP.set(key.toLowerCase(), entry);
  }
}

/** Resolve a client-supplied model id/slug/alias. Returns null when unknown. */
export function resolveModel(input) {
  if (!input || typeof input !== 'string') return null;
  return LOOKUP.get(input.trim().toLowerCase()) || null;
}

export function isAllowedModel(input) {
  return resolveModel(input) !== null;
}

export function allowedModelIds() {
  return MODELS.map((entry) => entry.id);
}

export function toPublicModel(entry) {
  const promptPerToken = perTokenString(entry.pricing.promptPerMTok);
  const completionPerToken = perTokenString(entry.pricing.completionPerMTok);
  return {
    id: entry.id,
    slug: entry.slug,
    aliases: [...entry.aliases],
    name: entry.name,
    provider: entry.provider,
    contextWindow: entry.contextWindow,
    maxOutput: entry.maxOutput,
    modality: entry.modality,
    capabilities: [...entry.capabilities],
    free: entry.free,
    recommended: entry.recommended,
    pricing: {
      currency: 'USD',
      unit: 'per_token',
      promptPerToken,
      completionPerToken,
      promptPerMTok: entry.pricing.promptPerMTok,
      completionPerMTok: entry.pricing.completionPerMTok
    }
  };
}

/** List models for the public catalogue endpoint. */
export function listModels({ freeOnly = false, provider = null } = {}) {
  return MODELS
    .filter((entry) => (freeOnly ? entry.free : true))
    .filter((entry) => (provider ? entry.provider.toLowerCase() === String(provider).toLowerCase() : true))
    .map(toPublicModel);
}

/** USD cost for a completed request using the catalogue price. */
export function costUsdFor(entry, promptTokens = 0, completionTokens = 0) {
  if (!entry) return 0;
  const p = Number(promptTokens) || 0;
  const c = Number(completionTokens) || 0;
  return round((p * entry.pricing.promptPerMTok + c * entry.pricing.completionPerMTok) / 1e6, 8);
}

/** Credits charged for a request. 1 credit == 1 USD. */
export function creditsForUsage(entry, promptTokens = 0, completionTokens = 0, creditsPerUsd = 1) {
  const cost = costUsdFor(entry, promptTokens, completionTokens);
  if (cost <= 0) return 0;
  // Charge in micro-credits so tiny requests are not silently free.
  return Math.max(0.000001, Math.ceil(cost * creditsPerUsd * 1e6) / 1e6);
}

/** Rough token estimate used for pre-flight balance checks. */
export function estimateTokens(value) {
  if (!value) return 0;
  const text = typeof value === 'string' ? value : JSON.stringify(value);
  return Math.max(1, Math.ceil(text.length / 4));
}

export function estimatePromptTokens(messages = []) {
  return messages.reduce((sum, message) => {
    const content = message?.content;
    const flat = Array.isArray(content)
      ? content.map((part) => (typeof part === 'string' ? part : part?.text || '')).join(' ')
      : content;
    return sum + estimateTokens(flat) + 4;
  }, 0);
}

function perTokenString(perMTok) {
  if (!perMTok) return '0';
  return (perMTok / 1e6)
    .toFixed(12)
    .replace(/0+$/, '')
    .replace(/\.$/, '');
}

function round(value, decimals) {
  const factor = 10 ** decimals;
  return Math.round(value * factor) / factor;
}
