import express from 'express';
import crypto from 'crypto';
import {
  allowedModelIds,
  costUsdFor,
  creditsForUsage,
  estimatePromptTokens,
  estimateTokens,
  listModels,
  resolveModel,
  PRICING_SNAPSHOT,
  PRICING_SOURCE
} from '../config/models.js';
import { CREDITS_PER_USD } from '../config/pricing.js';
import { ensureProfile, getCreditSummary, listUsage, recordUsage } from '../lib/credits.js';
import * as openrouter from '../lib/openrouter.js';
import { asyncHandler, requireUser } from '../lib/auth.js';
import { rateLimit } from '../lib/rateLimit.js';
import { AppError, insufficientCredits } from '../lib/errors.js';

export const aiRouter = express.Router();

const chatLimiter = rateLimit({
  scope: 'ai:chat',
  windowMs: 60_000,
  max: Number(process.env.AI_RATE_LIMIT_MAX || 60),
  message: 'Cloud AI rate limit exceeded. Slow down and try again.'
});

const catalogueLimiter = rateLimit({
  scope: 'ai:catalogue',
  windowMs: 60_000,
  max: Number(process.env.AI_CATALOGUE_RATE_LIMIT_MAX || 240)
});

const DEFAULT_MODEL = 'deepseek-v4.1-flash-free';
const ROLES = new Set(['system', 'developer', 'user', 'assistant', 'tool']);
const PASS_THROUGH_PARAMS = [
  'temperature', 'top_p', 'top_k', 'presence_penalty', 'frequency_penalty',
  'seed', 'stop', 'response_format', 'tools', 'tool_choice',
  'parallel_tool_calls', 'reasoning'
];

// GET /ai/models?free=true&provider=DeepSeek
aiRouter.get('/models', catalogueLimiter, (req, res) => {
  const freeOnly = String(req.query.free || '').toLowerCase() === 'true';
  const provider = req.query.provider || null;
  const models = listModels({ freeOnly, provider });
  res.json({
    models,
    count: models.length,
    pricing: {
      source: PRICING_SOURCE,
      snapshot: PRICING_SNAPSHOT,
      currency: 'USD',
      unit: 'per_1m_tokens',
      perTokenAlsoProvided: true,
      creditsPerUsd: CREDITS_PER_USD
    },
    allowlist: allowedModelIds()
  });
});

// POST /ai/estimate { model, messages | prompt_tokens, max_tokens }
aiRouter.post('/estimate', catalogueLimiter, asyncHandler(async (req, res) => {
  const body = req.body || {};
  const entry = resolveModel(body.model || DEFAULT_MODEL);
  if (!entry) {
    throw new AppError(400, 'model_not_allowed', 'Model is not in the ModelBench allowlist', {
      allowed: allowedModelIds()
    });
  }

  const promptTokens = Number.isFinite(Number(body.prompt_tokens))
    ? Number(body.prompt_tokens)
    : estimatePromptTokens(Array.isArray(body.messages) ? body.messages : []);
  const maxTokens = clampInt(body.max_tokens, 1, entry.maxOutput, 1024);
  const costUsd = costUsdFor(entry, promptTokens, maxTokens);
  const credits = creditsForUsage(entry, promptTokens, maxTokens);

  res.json({
    model: entry.id,
    free: entry.free,
    prompt_tokens: promptTokens,
    max_tokens: maxTokens,
    cost_usd: costUsd,
    credits,
    pricing: {
      promptPerMTok: entry.pricing.promptPerMTok,
      completionPerMTok: entry.pricing.completionPerMTok
    }
  });
}));

// GET /ai/usage — recent cloud AI usage for the authenticated user
aiRouter.get('/usage', asyncHandler(async (req, res) => {
  const user = await requireUser(req);
  const limit = Math.min(Number(req.query.limit) || 50, 200);
  const [usage, summary] = await Promise.all([
    listUsage(user.id, limit),
    getCreditSummary(user.id, { historyLimit: 0 })
  ]);
  res.json({ usage, balance: summary.balance, tier: summary.tier });
}));

// POST /ai/chat — OpenRouter proxy with allowlist, credits and SSE streaming
aiRouter.post('/chat', chatLimiter, asyncHandler(async (req, res) => {
  const user = await requireUser(req);
  const body = req.body || {};

  const entry = resolveModel(body.model || DEFAULT_MODEL);
  if (!entry) {
    throw new AppError(400, 'model_not_allowed', 'Model is not in the ModelBench allowlist', {
      allowed: allowedModelIds()
    });
  }

  const messages = validateMessages(body.messages);
  const stream = body.stream === true || body.stream === 'true';
  const maxTokens = clampInt(body.max_tokens ?? body.max_completion_tokens, 1, entry.maxOutput, 1024);
  const promptTokensEstimate = estimatePromptTokens(messages);
  const estimatedCredits = entry.free ? 0 : creditsForUsage(entry, promptTokensEstimate, maxTokens);

  const profile = await ensureProfile(user);
  const balance = Number(profile?.credits_balance || 0);
  if (balance < estimatedCredits) {
    throw insufficientCredits(balance, estimatedCredits);
  }

  const params = { max_tokens: maxTokens };
  for (const key of PASS_THROUGH_PARAMS) {
    if (body[key] !== undefined) params[key] = body[key];
  }

  const requestId = crypto.randomUUID();
  const useMock = !openrouter.isConfigured();

  if (useMock) {
    return stream
      ? streamMock({ req, res, entry, user, messages, promptTokensEstimate, maxTokens, requestId })
      : jsonMock({ res, entry, user, messages, promptTokensEstimate, maxTokens, requestId });
  }

  const upstream = await openrouter.createChatCompletion({
    model: entry.id,
    messages,
    stream,
    params
  });

  if (!stream) {
    const data = await upstream.json();
    const usage = normalizeUsage(data.usage, messages, extractContent(data));
    const costUsd = costUsdFor(entry, usage.promptTokens, usage.completionTokens);
    const credits = creditsForUsage(entry, usage.promptTokens, usage.completionTokens);
    const charge = await recordUsage({
      userId: user.id,
      model: entry.id,
      provider: entry.provider,
      promptTokens: usage.promptTokens,
      completionTokens: usage.completionTokens,
      costUsd,
      credits,
      requestId,
      status: 'ok',
      metadata: {
        stream: false,
        upstream_model: data.model || null,
        upstream_cost_usd: data.usage?.total_cost ?? data.usage?.cost ?? null
      }
    });

    return res.json({
      ...data,
      modelbench: {
        model: entry.id,
        display_model: entry.name,
        cost_usd: costUsd,
        credits_charged: charge.charged,
        balance: charge.balance,
        pricing: {
          promptPerMTok: entry.pricing.promptPerMTok,
          completionPerMTok: entry.pricing.completionPerMTok
        },
        request_id: requestId,
        mock: false
      }
    });
  }

  return proxyStream({ req, res, upstream, entry, user, promptTokensEstimate, requestId });
}));

/** Validate and normalise the OpenAI-style messages array. */
function validateMessages(messages) {
  if (!Array.isArray(messages) || messages.length === 0) {
    throw new AppError(400, 'invalid_messages', 'messages must be a non-empty array');
  }
  if (messages.length > 200) {
    throw new AppError(400, 'too_many_messages', 'At most 200 messages are allowed');
  }

  return messages.map((message, index) => {
    if (!message || typeof message !== 'object') {
      throw new AppError(400, 'invalid_message', `messages[${index}] must be an object`);
    }
    if (!ROLES.has(message.role)) {
      throw new AppError(400, 'invalid_role', `messages[${index}].role is invalid`);
    }
    const content = message.content;
    const contentOk = content === undefined || content === null
      || typeof content === 'string' || Array.isArray(content);
    if (!contentOk) {
      throw new AppError(400, 'invalid_content', `messages[${index}].content must be a string or array`);
    }
    return message;
  });
}

async function jsonMock({ res, entry, user, messages, promptTokensEstimate, maxTokens, requestId }) {
  const text = mockText(entry, messages);
  const completionTokens = estimateTokens(text);
  const usage = {
    promptTokens: promptTokensEstimate,
    completionTokens: Math.min(completionTokens, maxTokens)
  };
  const costUsd = costUsdFor(entry, usage.promptTokens, usage.completionTokens);
  const credits = creditsForUsage(entry, usage.promptTokens, usage.completionTokens);
  const charge = await recordUsage({
    userId: user.id,
    model: entry.id,
    provider: entry.provider,
    promptTokens: usage.promptTokens,
    completionTokens: usage.completionTokens,
    costUsd,
    credits,
    requestId,
    status: 'mock',
    metadata: { stream: false, mock: true }
  });

  res.json({
    id: `chatcmpl-mock-${requestId}`,
    object: 'chat.completion',
    created: Math.floor(Date.now() / 1000),
    model: entry.id,
    choices: [{
      index: 0,
      message: { role: 'assistant', content: text },
      finish_reason: 'stop'
    }],
    usage: {
      prompt_tokens: usage.promptTokens,
      completion_tokens: usage.completionTokens,
      total_tokens: usage.promptTokens + usage.completionTokens
    },
    modelbench: {
      model: entry.id,
      display_model: entry.name,
      cost_usd: costUsd,
      credits_charged: charge.charged,
      balance: charge.balance,
      pricing: {
        promptPerMTok: entry.pricing.promptPerMTok,
        completionPerMTok: entry.pricing.completionPerMTok
      },
      request_id: requestId,
      mock: true
    }
  });
}

async function streamMock({ res, entry, user, messages, promptTokensEstimate, maxTokens, requestId }) {
  const text = mockText(entry, messages);
  const created = Math.floor(Date.now() / 1000);
  const id = `chatcmpl-mock-${requestId}`;

  openSse(res);

  const words = text.split(/(\s+)/).filter(Boolean);
  const chunks = [];
  for (let i = 0; i < words.length; i += 4) {
    chunks.push(words.slice(i, i + 4).join(''));
  }

  for (let index = 0; index < chunks.length; index += 1) {
    if (res.writableEnded) break;
    res.write(`data: ${JSON.stringify({
      id,
      object: 'chat.completion.chunk',
      created,
      model: entry.id,
      choices: [{
        index: 0,
        delta: index === 0
          ? { role: 'assistant', content: chunks[index] }
          : { content: chunks[index] },
        finish_reason: null
      }]
    })}\n\n`);
  }

  const completionTokens = Math.min(estimateTokens(text), maxTokens);
  const usage = { promptTokens: promptTokensEstimate, completionTokens };
  const costUsd = costUsdFor(entry, usage.promptTokens, usage.completionTokens);
  const credits = creditsForUsage(entry, usage.promptTokens, usage.completionTokens);
  const charge = await recordUsage({
    userId: user.id,
    model: entry.id,
    provider: entry.provider,
    promptTokens: usage.promptTokens,
    completionTokens: usage.completionTokens,
    costUsd,
    credits,
    requestId,
    status: 'mock',
    metadata: { stream: true, mock: true }
  });

  res.write(`data: ${JSON.stringify({
    id,
    object: 'chat.completion.chunk',
    created,
    model: entry.id,
    choices: [{ index: 0, delta: {}, finish_reason: 'stop' }],
    usage: {
      prompt_tokens: usage.promptTokens,
      completion_tokens: usage.completionTokens,
      total_tokens: usage.promptTokens + usage.completionTokens
    }
  })}\n\n`);

  res.write(`event: modelbench.usage\ndata: ${JSON.stringify({
    model: entry.id,
    prompt_tokens: usage.promptTokens,
    completion_tokens: usage.completionTokens,
    cost_usd: costUsd,
    credits_charged: charge.charged,
    balance: charge.balance,
    request_id: requestId,
    mock: true
  })}\n\n`);

  res.write('data: [DONE]\n\n');
  res.end();
}

async function proxyStream({ req, res, upstream, entry, user, promptTokensEstimate, requestId }) {
  openSse(res);

  const reader = upstream.body?.getReader();
  if (!reader) {
    res.write(`event: error\ndata: ${JSON.stringify({ error: 'empty_upstream_stream' })}\n\n`);
    res.end();
    return;
  }

  const decoder = new TextDecoder();
  let buffer = '';
  let completionText = '';
  let usage = null;
  let clientClosed = false;
  let streamError = null;

  const onClose = () => {
    if (!res.writableEnded) {
      clientClosed = true;
      reader.cancel().catch(() => {});
    }
  };
  res.on('close', onClose);

  const write = (chunk) => {
    if (clientClosed || res.writableEnded) return;
    try {
      res.write(chunk);
    } catch {
      clientClosed = true;
    }
  };

  const consumeLine = (line) => {
    if (!line.startsWith('data:')) {
      // Forward comments, event names and blank separators unchanged.
      write(`${line}\n`);
      return;
    }

    const payload = line.slice(5).trim();
    if (payload === '[DONE]') {
      // Suppress upstream's terminator; we send our own after settlement.
      return;
    }

    try {
      const json = JSON.parse(payload);
      if (json.usage) usage = json.usage;
      const delta = json.choices?.[0]?.delta?.content;
      if (typeof delta === 'string') completionText += delta;
    } catch {
      // Forward non-JSON data lines untouched.
    }
    write(`${line}\n`);
  };

  try {
    while (true) {
      const { done, value } = await reader.read();
      if (done) break;
      buffer += decoder.decode(value, { stream: true });

      let newline;
      while ((newline = buffer.indexOf('\n')) >= 0) {
        const line = buffer.slice(0, newline).replace(/\r$/, '');
        buffer = buffer.slice(newline + 1);
        consumeLine(line);
      }
    }
    if (buffer.length) {
      consumeLine(buffer.replace(/\r$/, ''));
      buffer = '';
    }
  } catch (error) {
    if (!clientClosed) {
      streamError = error.message;
      write(`event: error\ndata: ${JSON.stringify({ error: 'upstream_stream_error', message: error.message })}\n\n`);
    }
  } finally {
    res.off('close', onClose);
  }

  const finalUsage = normalizeUsage(usage, [], completionText, promptTokensEstimate);
  const costUsd = costUsdFor(entry, finalUsage.promptTokens, finalUsage.completionTokens);
  const credits = creditsForUsage(entry, finalUsage.promptTokens, finalUsage.completionTokens);

  let charge = { charged: 0, balance: null };
  try {
    charge = await recordUsage({
      userId: user.id,
      model: entry.id,
      provider: entry.provider,
      promptTokens: finalUsage.promptTokens,
      completionTokens: finalUsage.completionTokens,
      costUsd,
      credits,
      requestId,
      status: clientClosed ? 'canceled' : (streamError ? 'error' : 'ok'),
      metadata: {
        stream: true,
        client_closed: clientClosed,
        upstream_error: streamError
      }
    });
  } catch (error) {
    write(`event: error\ndata: ${JSON.stringify({ error: 'credit_settlement_failed', message: error.message })}\n\n`);
  }

  write(`event: modelbench.usage\ndata: ${JSON.stringify({
    model: entry.id,
    prompt_tokens: finalUsage.promptTokens,
    completion_tokens: finalUsage.completionTokens,
    cost_usd: costUsd,
    credits_charged: charge.charged,
    balance: charge.balance,
    request_id: requestId,
    mock: false
  })}\n\n`);
  write('data: [DONE]\n\n');

  if (!res.writableEnded) res.end();
}

function openSse(res) {
  res.status(200);
  res.setHeader('Content-Type', 'text/event-stream; charset=utf-8');
  res.setHeader('Cache-Control', 'no-cache, no-transform');
  res.setHeader('Connection', 'keep-alive');
  res.setHeader('X-Accel-Buffering', 'no');
  if (typeof res.flushHeaders === 'function') res.flushHeaders();
}

function normalizeUsage(usage, messages, content = '', promptFallback = null) {
  const promptTokens = Number(usage?.prompt_tokens ?? usage?.promptTokens);
  const completionTokens = Number(usage?.completion_tokens ?? usage?.completionTokens);
  return {
    promptTokens: Number.isFinite(promptTokens) && promptTokens > 0
      ? Math.trunc(promptTokens)
      : (promptFallback ?? estimatePromptTokens(messages)),
    completionTokens: Number.isFinite(completionTokens) && completionTokens > 0
      ? Math.trunc(completionTokens)
      : estimateTokens(content)
  };
}

function extractContent(data) {
  const content = data?.choices?.[0]?.message?.content;
  return typeof content === 'string' ? content : '';
}

function mockText(entry, messages) {
  const lastUser = [...messages].reverse().find((message) => message.role === 'user');
  let prompt = '';
  if (typeof lastUser?.content === 'string') {
    prompt = lastUser.content;
  } else if (Array.isArray(lastUser?.content)) {
    prompt = lastUser.content
      .map((part) => (typeof part === 'string' ? part : part?.text || ''))
      .join(' ');
  }
  prompt = prompt.replace(/\s+/g, ' ').trim().slice(0, 180);
  const suffix = prompt ? ` You said: "${prompt}".` : '';
  return `[mock] ${entry.name} is running in mock mode.${suffix} Set OPENROUTER_API_KEY on the server to reach the real model.`;
}

function clampInt(value, min, max, fallback) {
  const number = Number(value);
  if (!Number.isFinite(number)) return fallback;
  return Math.max(min, Math.min(Math.trunc(number), max));
}
