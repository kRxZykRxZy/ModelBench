import express from 'express';
import cors from 'cors';
import dotenv from 'dotenv';

dotenv.config();

import { authRouter } from './routes/auth.js';
import { billingRouter } from './routes/billing.js';
import { userRouter } from './routes/user.js';

const app = express();
const PORT = process.env.PORT || 3000;

// CORS: allow the desktop app (and localhost dev) to call the API
app.use(cors({
  origin: (origin, cb) => cb(null, true),
  credentials: true
}));

// Webhook needs raw body for signature verification
app.use('/billing/webhook', express.raw({ type: 'application/json' }));
app.use(express.json());

app.get('/health', (req, res) => res.json({ ok: true, service: 'modelbench-server' }));

app.use('/auth', authRouter);
app.use('/user', userRouter);
app.use('/billing', billingRouter);

app.listen(PORT, () => {
  console.log(`ModelBench server listening on http://localhost:${PORT}`);
});
