/**
 * Zarlino Executive OS — Worker entry point.
 *
 *   /api/*        → Executive REST API (spec §30)
 *   /agents/*     → Cloudflare Agents SDK routing (WebSocket/chat path)
 *   everything else → static assets from the web console build (web/dist)
 */

import { routeAgentRequest } from "agents";
import { ExecutiveAgent, type ExecutiveEnv } from "./agent/executive-agent";
import { handleApi } from "./api/router";

export { ExecutiveAgent };

export default {
  async fetch(request: Request, env: ExecutiveEnv, ctx: ExecutionContext): Promise<Response> {
    const url = new URL(request.url);

    // REST API.
    if (url.pathname.startsWith("/api/")) {
      return handleApi(request, env);
    }

    // Agents SDK routing (agent instances, WebSockets).
    const routed = await routeAgentRequest(request, env);
    if (routed) return routed;

    // Serve the built web console.
    if (env.ASSETS) {
      return env.ASSETS.fetch(request);
    }

    return new Response("Zarlino Executive OS — build the web console with `npm --prefix web run build`.", {
      status: 404,
      headers: { "Content-Type": "text/plain" },
    });
  },
} satisfies ExportedHandler<ExecutiveEnv>;
