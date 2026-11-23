import '../env.js';
import { OPENROUTER_BASE_URL } from '../config/models.js';
import { AppError } from './errors.js';

/** True when a real OpenRouter key is present and AI_MOCK is not forced. */
export function isConfigured() {
  if (String(process.env.AI_MOCK || '').toLowerCase() === 'true') return false;
  return Boolean(process.env.OPENROUTER_API_KEY);
}

/**
 * Create a chat completion request against OpenRouter.
 * The caller owns the response body (JSON or SSE) and must consume it.
 */
export async function createChatCompletion({ model, messages, stream = false, params = {}, signal }) {
  const apiKey = process.env.OPENROUTER_API_KEY;
  if (!apiKey) {
    throw new AppError(503, 'openrouter_not_configured', 'OPENROUTER_API_KEY is not configured');
  }

  const body = {
    model,
    messages,
    stream: Boolean(stream),
    ...params
  };
  if (stream) {
    body.stream_options = { include_usage: true };
  }

  const timeoutMs = Number(process.env.OPENROUTER_TIMEOUT_MS || 180_000);
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs);
  if (signal) {
    if (signal.aborted) controller.abort();
    else signal.addEventListener('abort', () => controller.abort(), { once: true });
  }

  try {
    const response = await fetch(`${OPENROUTER_BASE_URL}/chat/completions`, {
      method: 'POST',
      signal: controller.signal,
      headers: {
        Authorization: `Bearer ${apiKey}`,
        'Content-Type': 'application/json',
        'HTTP-Referer': process.env.SITE_URL || 'http://localhost:3000',
        'X-Title': 'ModelBench'
      },
      body: JSON.stringify(body)
    });

    if (!response.ok) {
      const text = await response.text().catch(() => '');
      throw new AppError(
        502,
        'openrouter_error',
        `OpenRouter returned ${response.status}: ${text.slice(0, 400) || response.statusText}`
      );
    }
    return response;
  } catch (error) {
    if (error.name === 'AbortError') {
      throw new AppError(504, 'openrouter_timeout', 'OpenRouter request timed out');
    }
    if (error instanceof AppError) throw error;
    throw new AppError(502, 'openrouter_error', error.message);
  } finally {
    clearTimeout(timer);
  }
}
