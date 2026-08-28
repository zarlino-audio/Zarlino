/**
 * Founder password auth (username + password with forced first-visit setup).
 *
 * Passwords are hashed with PBKDF2 (SHA-256, 100k iterations, per-user random
 * salt) via Web Crypto — available in both Workers and Node (for tests).
 * Sessions are random bearer tokens stored in SQLite with an expiry.
 *
 * The legacy FOUNDER_AUTH_TOKEN env secret remains valid as a second bearer
 * credential (for scripts/APIs), but the console signs in with username +
 * password.
 */

const PBKDF2_ITERATIONS = 100_000;
const KEY_LENGTH_BITS = 256;
const SALT_BYTES = 16;
const TOKEN_BYTES = 32;
const SESSION_TTL_MS = 30 * 24 * 60 * 60 * 1000; // 30 days

export const USERNAME_MIN = 3;
export const USERNAME_MAX = 40;
export const PASSWORD_MIN = 8;
export const PASSWORD_MAX = 128;

export interface PasswordHash {
  /** Stored format: pbkdf2$<iterations>$<saltB64>$<hashB64> */
  encoded: string;
}

function toBase64(bytes: Uint8Array): string {
  let bin = "";
  for (const b of bytes) bin += String.fromCharCode(b);
  return btoa(bin);
}

function fromBase64(b64: string): Uint8Array {
  const bin = atob(b64);
  const bytes = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
  return bytes;
}

export function timingSafeEqual(a: string, b: string): boolean {
  if (a.length !== b.length) return false;
  let diff = 0;
  for (let i = 0; i < a.length; i++) diff |= a.charCodeAt(i) ^ b.charCodeAt(i);
  return diff === 0;
}

export async function hashPassword(password: string, saltB64?: string): Promise<PasswordHash> {
  const enc = new TextEncoder();
  const salt = saltB64 ? fromBase64(saltB64) : crypto.getRandomValues(new Uint8Array(SALT_BYTES));
  const keyMaterial = await crypto.subtle.importKey("raw", enc.encode(password), "PBKDF2", false, ["deriveBits"]);
  const bits = await crypto.subtle.deriveBits(
    { name: "PBKDF2", salt: salt.buffer as ArrayBuffer, iterations: PBKDF2_ITERATIONS, hash: "SHA-256" },
    keyMaterial,
    KEY_LENGTH_BITS,
  );
  const hash = new Uint8Array(bits);
  return { encoded: `pbkdf2$${PBKDF2_ITERATIONS}$${toBase64(salt)}$${toBase64(hash)}` };
}

export async function verifyPassword(password: string, stored: string): Promise<boolean> {
  const parts = stored.split("$");
  if (parts.length !== 4 || parts[0] !== "pbkdf2") return false;
  const iterations = Number(parts[1]);
  const saltB64 = parts[2];
  const expectedHash = parts[3];
  if (!saltB64 || !expectedHash || !Number.isInteger(iterations) || iterations < 1) return false;
  const { encoded } = await hashWithIterations(password, saltB64, iterations);
  return timingSafeEqual(encoded.split("$")[3]!, expectedHash);
}

async function hashWithIterations(password: string, saltB64: string, iterations: number): Promise<PasswordHash> {
  const enc = new TextEncoder();
  const salt = fromBase64(saltB64);
  const keyMaterial = await crypto.subtle.importKey("raw", enc.encode(password), "PBKDF2", false, ["deriveBits"]);
  const bits = await crypto.subtle.deriveBits(
    { name: "PBKDF2", salt: salt.buffer as ArrayBuffer, iterations, hash: "SHA-256" },
    keyMaterial,
    KEY_LENGTH_BITS,
  );
  const hash = new Uint8Array(bits);
  return { encoded: `pbkdf2$${iterations}$${saltB64}$${toBase64(hash)}` };
}

export function generateToken(): string {
  const bytes = crypto.getRandomValues(new Uint8Array(TOKEN_BYTES));
  return Array.from(bytes).map((b) => b.toString(16).padStart(2, "0")).join("");
}

export function sessionExpiry(now = Date.now()): string {
  return new Date(now + SESSION_TTL_MS).toISOString();
}

export function isExpired(expiresAt: string, now = Date.now()): boolean {
  const t = new Date(expiresAt).getTime();
  return Number.isNaN(t) ? true : t < now;
}

export function validateUsername(username: string): string | null {
  const u = username.trim();
  if (u.length < USERNAME_MIN || u.length > USERNAME_MAX) {
    return `Username must be ${USERNAME_MIN}-${USERNAME_MAX} characters.`;
  }
  if (!/^[a-zA-Z0-9_.-]+$/.test(u)) {
    return "Username may only contain letters, digits, '.', '_' or '-'.";
  }
  return null;
}

export function validatePassword(password: string): string | null {
  if (password.length < PASSWORD_MIN) {
    return `Password must be at least ${PASSWORD_MIN} characters.`;
  }
  if (password.length > PASSWORD_MAX) {
    return `Password must be at most ${PASSWORD_MAX} characters.`;
  }
  return null;
}
