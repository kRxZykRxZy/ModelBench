import './env.js';
import { createApp } from './app.js';
import { isConfigured } from './lib/supabase.js';

const app = createApp();
const port = Number(process.env.PORT || 3000);

const server = app.listen(port, () => {
  console.log(`ModelBench server listening on http://localhost:${port}`);
  console.log(`  database: ${isConfigured() ? 'Supabase' : 'mock (in-memory)'}`);
  console.log(`  cloud AI: ${process.env.OPENROUTER_API_KEY && process.env.AI_MOCK !== 'true' ? 'OpenRouter' : 'mock'}`);
  console.log(`  billing:  ${process.env.WHOP_API_KEY ? 'Whop' : 'mock'}`);
});

function shutdown(signal) {
  console.log(`\n${signal} received, shutting down...`);
  server.close(() => process.exit(0));
  setTimeout(() => process.exit(1), 10_000).unref();
}

process.on('SIGINT', () => shutdown('SIGINT'));
process.on('SIGTERM', () => shutdown('SIGTERM'));

export { app, server };
