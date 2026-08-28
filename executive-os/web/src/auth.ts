/**
 * Founder auth for the Executive console.
 *
 * The live API requires `Authorization: Bearer <FOUNDER_AUTH_TOKEN>`. The
 * token is stored only in the browser's localStorage (never sent to any other
 * origin — the console and API share the same origin).
 */

const TOKEN_KEY = "zarlino_exec_token";
const USER_KEY = "zarlino_exec_user";

export function getToken(): string {
  try {
    return localStorage.getItem(TOKEN_KEY) ?? "";
  } catch {
    return "";
  }
}

export function setToken(token: string): void {
  try {
    localStorage.setItem(TOKEN_KEY, token.trim());
  } catch {
    /* storage unavailable */
  }
}

export function getUsername(): string {
  try {
    return localStorage.getItem(USER_KEY) ?? "";
  } catch {
    return "";
  }
}

export function setUsername(username: string): void {
  try {
    localStorage.setItem(USER_KEY, username);
  } catch {
    /* storage unavailable */
  }
}

export function clearToken(): void {
  try {
    localStorage.removeItem(TOKEN_KEY);
  } catch {
    /* storage unavailable */
  }
}

export function clearAuth(): void {
  clearToken();
  try {
    localStorage.removeItem(USER_KEY);
  } catch {
    /* storage unavailable */
  }
}

export function isLoggedIn(): boolean {
  return getToken().length > 0;
}

export function authHeaders(): Record<string, string> {
  const token = getToken();
  return token ? { Authorization: `Bearer ${token}` } : {};
}
