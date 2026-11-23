/** Small typed error used by routes and serialized by the error middleware. */
export class AppError extends Error {
  constructor(status, code, message, extra = {}) {
    super(message);
    this.name = 'AppError';
    this.status = status;
    this.code = code;
    Object.assign(this, extra);
  }

  toJSON() {
    const { status, code, message, ...rest } = this;
    return { error: code, message, ...rest };
  }
}

export function insufficientCredits(balance, required = null) {
  return new AppError(
    402,
    'insufficient_credits',
    'Not enough credits. Purchase a plan or top up to continue using cloud AI.',
    { balance, ...(required === null ? {} : { required }) }
  );
}

export function badRequest(code, message, extra) {
  return new AppError(400, code, message, extra);
}

export function unauthorized(message = 'Authentication required') {
  return new AppError(401, 'unauthorized', message);
}

export function forbidden(message = 'Not allowed') {
  return new AppError(403, 'forbidden', message);
}

export function notFound(message = 'Not found') {
  return new AppError(404, 'not_found', message);
}
