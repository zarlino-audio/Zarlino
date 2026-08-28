/**
 * API authentication (spec §24).
 *
 * Every /api/* request requires `Authorization: Bearer <FOUNDER_AUTH_TOKEN>`.
 * In production, a missing token denies all requests. In local development
 * (ENVIRONMENT=development) with no token configured, requests are allowed so
 * the system is usable without setup — this is documented and must not be
 * shipped to production.
 */

import type { ExecutiveEnv } from "../agent/executive-agent";

export function isAuthorized(request: Request, env: ExecutiveEnv): boolean {
  const token = env.FOUNDER_AUTH_TOKEN;
  const environment = env.ENVIRONMENT;

  if (!token) {
    // Dev-only convenience. Never allow unauthenticated in production.
    return environment === "development" || environment === "local" || environment === undefined;
  }

  const header = request.headers.get("Authorization") ?? "";
  return header === `Bearer ${token}`;
}

export function unauthorized(): Response {
  return json({ error: "Unauthorized" }, 401);
}

export function json(body: unknown, status = 200): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: {
      "Content-Type": "application/json; charset=utf-8",
      "Cache-Control": "no-store",
      "Access-Control-Allow-Origin": "*",
      "Access-Control-Allow-Methods": "GET, POST, PATCH, DELETE, OPTIONS",
      "Access-Control-Allow-Headers": "Content-Type, Authorization",
    },
  });
}

export async function readJson<T>(request: Request): Promise<T | { error: string }> {
  try {
    return (await request.json()) as T;
  } catch {
    return { error: "Invalid JSON body" };
  }
}

export function error(message: string, status = 400): Response {
  return json({ error: message }, status);
}
