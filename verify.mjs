import './env.js';
import * as pricing from './src/config/pricing.js';
import * as models from './src/config/models.js';

// Check pricing
console.log('=== Pricing ===');
console.log('CREDITS_PER_USD:', pricing.CREDITS_PER_USD);
console.log('free:', pricing.PLANS.free.tier, pricing.PLANS.free.priceUsd, pricing.PLANS.free.credits);
console.log('hobby:', pricing.PLANS.hobby.tier, pricing.PLANS.hobby.priceUsd, pricing.PLANS.hobby.credits);
console.log('scale:', pricing.PLANS.scale.tier, pricing.PLANS.scale.priceUsd, pricing.PLANS.scale.credits);
console.log('teams:', pricing.PLANS.teams.tier, pricing.PLANS.teams.priceUsd, pricing.PLANS.teams.credits);

// Check models
console.log('\n=== Models ===');
console.log('Total models:', models.MODELS.length);

// Check for forbidden models
const forbidden = /(?:^|[^a-z])(gpt|chatgpt|claude|openai|anthropic|gemini|google)(?:[^a-z]|$)/i;
let foundForbidden = false;
for (const entry of models.MODELS) {
  const haystack = `${entry.id} ${entry.name} ${entry.provider}`;
  if (forbidden.test(haystack)) {
    console.log('FORBIDDEN:', entry.id, entry.provider);
    foundForbidden = true;
  }
}
console.log('No forbidden models:', !foundForbidden);

// Check specific model types
const providers = [...new Set(models.MODELS.map(m => m.provider))];
console.log('Providers:', providers);

// Check HY4 and Nemotron presence
const hasHY4 = models.MODELS.some(m => m.id.includes('hunyuan-4') || m.slug.includes('hy4'));
const hasNemotron = models.MODELS.some(m => m.id.includes('nemotron') || m.slug.includes('nemotron'));
console.log('Has HY4 model:', hasHY4);
console.log('Has Nemotron model:', hasNemotron);

// Check ai.js proxy
console.log('\n=== AI Router ===');
// Just verify the file can be imported
import('./src/routes/ai.js').then(aiRouter => {
  console.log('ai.js imported successfully');
}).catch(err => {
  console.error('ai.js error:', err.message);
});

// Check whop webhook signature verification
console.log('\n=== Webhook Signature ===');
import('./src/lib/whop.js').then(whop => {
  console.log('verifyWebhookSignature:', typeof whop.verifyWebhookSignature);
  console.log('safeEqual:', typeof whop.safeEqual);
  console.log('timingSafeEqual used:', whop.safeEqual.toString().includes('timingSafeEqual'));
}).catch(err => {
  console.error('whop.js error:', err.message);
});

// Check index.js
console.log('\n=== Index ===');
import('./src/index.js').then(app => {
  console.log('index.js imported successfully');
}).catch(err => {
  console.error('index.js error:', err.message);
});

// Check billing.js
console.log('\n=== Billing ===');
import('./src/routes/billing.js').then(billing => {
  console.log('billing.js imported successfully');
}).catch(err => {
  console.error('billing.js error:', err.message);
});