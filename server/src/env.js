/**
 * Load .env before any module reads process.env.
 *
 * ESM evaluates imported modules before the importing module's body, so calling
 * dotenv.config() in index.js would run too late. Every module that reads env at
 * import time imports this file first instead.
 */
import dotenv from 'dotenv';

dotenv.config();
