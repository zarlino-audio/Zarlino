/**
 * REST API router (spec §30). All routes are typed at the edge with zod.
 * Every route authenticates the Founder token, then calls the ExecutiveAgent
 * via Durable Object RPC. IMPORTANT: every agent call is a Promise (RPC) and
 * MUST be awaited.
 */

import { z } from "zod";
import { getAgentByName } from "agents";
import type { ExecutiveAgent, ExecutiveEnv } from "../agent/executive-agent";
import { error, json, readJson, unauthorized } from "./auth";

const schemas = {
  chat: z.object({ message: z.string().min(1).max(8000), sessionId: z.string().max(200).optional() }),
  chatClear: z.object({ sessionId: z.string().min(1).max(200) }),
  setup: z.object({ username: z.string().min(1).max(40), password: z.string().min(8).max(128) }),
  login: z.object({ username: z.string().min(1).max(40), password: z.string().min(1).max(128) }),
  logout: z.object({ token: z.string().optional() }),
  password: z.object({ currentPassword: z.string().min(1).max(128), newPassword: z.string().min(8).max(128) }),
  decision: z.object({
    problem: z.string().min(1).max(8000),
    categoryHint: z.enum(["STRATEGIC", "FINANCIAL", "TECHNICAL", "OPERATIONAL", "MARKETING"]).optional(),
    proposedAction: z.string().max(200).optional(),
    estimatedCost: z.number().nonnegative().optional(),
    options: z.array(z.object({ id: z.string(), label: z.string(), description: z.string().optional(), cost: z.number().optional(), reversible: z.boolean().optional() })).optional(),
  }),
  taskCreate: z.object({ title: z.string().min(1).max(200), description: z.string().max(2000).optional(), priority: z.enum(["P0", "P1", "P2", "P3"]).optional(), projectId: z.string().optional(), dueDate: z.string().optional(), autoRun: z.boolean().optional() }),
  taskPatch: z.object({ status: z.enum(["OPEN", "IN_PROGRESS", "BLOCKED", "DONE", "CANCELLED"]).optional(), priority: z.enum(["P0", "P1", "P2", "P3"]).optional(), assigneePersonId: z.string().optional(), dueDate: z.string().optional(), autoRun: z.boolean().optional() }),
  approve: z.object({ note: z.string().max(2000).optional() }),
  memory: z.object({ layer: z.enum(["L0", "L1", "L2", "L3"]), kind: z.enum(["FACT", "INFERENCE", "ASSUMPTION", "ESTIMATE", "UNKNOWN", "LESSON", "POLICY"]), title: z.string().min(1).max(200), body: z.string().min(1).max(8000), importance: z.number().min(0).max(1).optional() }),
  doctrinePropose: z.object({ code: z.string().min(1).max(32), principle: z.string().min(1).max(1000), rationale: z.string().max(2000).optional(), category: z.string().max(64).optional() }),
  tool: z.object({ name: z.string().min(1).max(128), input: z.unknown() }),
  gatePatch: z.object({
    status: z.enum(["NOT_STARTED", "IN_PROGRESS", "PASSED", "FAILED", "BLOCKED", "SHIPPED"]).optional(),
    note: z.string().max(1000).optional(),
  }),
  ship: z.object({ pluginId: z.string().min(1).max(64), version: z.string().max(32).optional(), note: z.string().max(500).optional() }),
  agentRun: z.object({ prompt: z.string().min(1).max(8000), taskId: z.string().max(64).optional(), maxIterations: z.number().int().min(1).max(1000).optional() }),
  agentContinue: z.object({ runId: z.string().min(1).max(64) }),
  localTaskMeta: z.object({
    goal: z.string().max(2000).optional(),
    context: z.string().max(4000).optional(),
    acceptance: z.string().max(2000).optional(),
    deliverable: z.string().max(1000).optional(),
  }),
  localTaskCreate: z.object({ prompt: z.string().min(1).max(8000), taskId: z.string().max(64).optional(), meta: z.object({
    goal: z.string().max(2000).optional(),
    context: z.string().max(4000).optional(),
    acceptance: z.string().max(2000).optional(),
    deliverable: z.string().max(1000).optional(),
  }).optional() }),
  localTaskClaim: z.object({ claimer: z.string().min(1).max(64) }),
  localTaskResult: z.object({ status: z.enum(["RUNNING", "DONE", "FAILED"]), result: z.string().max(50_000).optional(), error: z.string().max(5000).optional() }),
  fileUpload: z.object({ name: z.string().min(1).max(255), mime: z.string().max(128).optional(), dataBase64: z.string().min(1) }),
  snapshot: z.object({ cash: z.number().optional(), revenue: z.number().optional(), expenses: z.number().optional(), period: z.string().max(16).optional(), confidence: z.enum(["FACT", "INFERENCE", "ASSUMPTION", "ESTIMATE", "UNKNOWN"]).optional(), notes: z.string().max(1000).optional() }),
  pause: z.object({ reason: z.string().max(500).optional() }),
  autonomy: z.object({ mode: z.enum(["CONTROLLED", "APPROVAL_ONLY", "PAUSED"]) }),
  delegate: z.object({ enabled: z.boolean() }),
};

/**
 * Get the single "zarlino" ExecutiveAgent stub via standard Durable Object RPC.
 * (Plain DO RPC is used instead of the Agents SDK getAgentByName proxy so every
 * public method on the agent is directly callable.)
 */
function getExecutiveAgent(env: ExecutiveEnv): ExecutiveAgent {
  const id = env.ExecutiveAgent.idFromName("zarlino");
  return env.ExecutiveAgent.get(id) as unknown as ExecutiveAgent;
}

export async function handleApi(request: Request, env: ExecutiveEnv): Promise<Response> {
  if (request.method === "OPTIONS") return json({ ok: true });

  const parts = new URL(request.url).pathname.split("/").filter(Boolean); // e.g. ["api","decisions","abc123"]

  try {
    // DO RPC to the single "zarlino" executive instance. Every call below
    // MUST be awaited — RPC methods return Promises.
    const agent = (await getAgentByName(env.ExecutiveAgent as never, "zarlino")) as unknown as ExecutiveAgent;

    // /api/auth/* is reachable without a session — those endpoints validate
    // themselves (first-visit setup and sign-in).
    if (parts[0] === "api" && parts[1] === "auth") {
      return await routeAuth(agent, request, parts);
    }

    // Everything else requires a valid bearer: a session token or the
    // FOUNDER_AUTH_TOKEN env secret.
    if (!(await isAuthorizedRequest(request, env, agent))) {
      return unauthorized();
    }

    return await route(agent, request, parts);
  } catch (err) {
    const message = err instanceof Error ? err.message : String(err);
    return error(message, 500);
  }
}

async function isAuthorizedRequest(request: Request, env: ExecutiveEnv, agent: ExecutiveAgent): Promise<boolean> {
  const header = request.headers.get("Authorization") ?? "";
  if (!header.startsWith("Bearer ")) {
    // Dev-only convenience when no token is configured (never in production).
    if (!env.FOUNDER_AUTH_TOKEN) {
      const environment = env.ENVIRONMENT;
      return environment === "development" || environment === "local" || environment === undefined;
    }
    return false;
  }
  const token = header.slice(7).trim();
  const envToken = env.FOUNDER_AUTH_TOKEN;
  if (envToken && token === envToken) return true;
  return agent.isSessionValid(token);
}

/** /api/auth/* — first-visit setup, sign-in, sign-out, change password. */
async function routeAuth(agent: ExecutiveAgent, request: Request, parts: string[]): Promise<Response> {
  const method = request.method;
  const sub = parts[2];
  const body = await readJson<Record<string, unknown>>(request);

  if (sub === "status" && method === "GET") {
    return json(await agent.getAuthStatus());
  }
  if (sub === "setup" && method === "POST") {
    const parsed = schemas.setup.safeParse(body);
    if (!parsed.success) return error(`Invalid body: ${parsed.error.message}`);
    try {
      return json(await agent.setupFounderCredentials(parsed.data.username, parsed.data.password));
    } catch (err) {
      return error(err instanceof Error ? err.message : String(err), 400);
    }
  }
  if (sub === "login" && method === "POST") {
    const parsed = schemas.login.safeParse(body);
    if (!parsed.success) return error(`Invalid body: ${parsed.error.message}`);
    try {
      return json(await agent.login(parsed.data.username, parsed.data.password));
    } catch (err) {
      return error(err instanceof Error ? err.message : String(err), 401);
    }
  }
  if (sub === "logout" && method === "POST") {
    const parsed = schemas.logout.safeParse(body);
    return json(await agent.logout(parsed.success ? parsed.data.token ?? "" : ""));
  }
  if (sub === "password" && method === "POST") {
    const parsed = schemas.password.safeParse(body);
    if (!parsed.success) return error(`Invalid body: ${parsed.error.message}`);
    try {
      return json(await agent.changePassword(parsed.data.currentPassword, parsed.data.newPassword));
    } catch (err) {
      return error(err instanceof Error ? err.message : String(err), 400);
    }
  }
  return error("Not found", 404);
}

async function route(agent: ExecutiveAgent, request: Request, parts: string[]): Promise<Response> {
  const method = request.method;
  const body = await readJson<Record<string, unknown>>(request);

  // /api/chat
  if (match(parts, ["api", "chat"]) && method === "POST") {
    const parsed = schemas.chat.safeParse(body);
    if (!parsed.success) return error(`Invalid body: ${parsed.error.message}`);
    return json(await agent.ask({ message: parsed.data.message, sessionId: parsed.data.sessionId }));
  }

  // /api/executive/*
  if (parts[0] === "api" && parts[1] === "executive") {
    const sub = parts[2];
    if (!sub) {
      return json({
        state: await agent.overview(),
        emergency: await agent.getEmergencyState(),
        schedules: await agent.getScheduleStatus(),
      });
    }
    switch (sub) {
      case "state": return json({ state: await agent.overview(), emergency: await agent.getEmergencyState() });
      case "overview": return json(await agent.overview());
      case "schedules": return json(await agent.getScheduleStatus());
      case "pause": {
        const parsed = schemas.pause.safeParse(body);
        return json(await agent.pause(parsed.success ? parsed.data.reason ?? "Paused via console" : "Paused via console"));
      }
      case "resume": return json(await agent.resume());
      case "spending": {
        if (method !== "POST") return error("Method not allowed", 405);
        return json(await agent.setSpendingDisabled((body as Record<string, unknown> | undefined)?.disabled === true));
      }
      case "deployments": {
        if (method !== "POST") return error("Method not allowed", 405);
        return json(await agent.setDeploymentsDisabled((body as Record<string, unknown> | undefined)?.disabled === true));
      }
      case "autonomy": {
        const parsed = schemas.autonomy.safeParse(body);
        if (!parsed.success) return error(`Invalid body: ${parsed.error.message}`);
        return json(await agent.setAutonomyMode(parsed.data.mode));
      }
      case "delegate": {
        const parsed = schemas.delegate.safeParse(body);
        if (!parsed.success) return error(`Invalid body: ${parsed.error.message}`);
        return json(await agent.setDelegateMode(parsed.data.enabled));
      }
      case "fullaccess": {
        const parsed = schemas.delegate.safeParse(body);
        if (!parsed.success) return error(`Invalid body: ${parsed.error.message}`);
        return json(await agent.setFullAccess(parsed.data.enabled));
      }
      case "brief": return json({ brief: await agent.runDailyBrief() });
      case "review": return json({ review: await agent.runWeeklyReview() });
      default: return error(`Unknown executive endpoint: ${sub}`, 404);
    }
  }

  // /api/decisions
  if (match(parts, ["api", "decisions"])) {
    if (method === "GET") {
      const status = urlSearch(request, "status");
      return json(await agent.listDecisions(status ? { status } : undefined));
    }
    if (method === "POST") {
      const parsed = schemas.decision.safeParse(body);
      if (!parsed.success) return error(`Invalid body: ${parsed.error.message}`);
      return json(await agent.runDecision({
        problem: parsed.data.problem,
        categoryHint: parsed.data.categoryHint,
        proposedAction: parsed.data.proposedAction,
        estimatedCost: parsed.data.estimatedCost,
        options: parsed.data.options?.map((o) => ({ ...o, reversible: o.reversible ?? true })),
        source: "founder",
      }));
    }
    return error("Method not allowed", 405);
  }

  // /api/decisions/:id
  if (match(parts, ["api", "decisions", ":id"])) {
    if (method === "GET") {
      const decision = await agent.getDecision(parts[2]!);
      return decision ? json(decision) : error("Decision not found", 404);
    }
    return error("Method not allowed", 405);
  }

  // /api/tasks
  if (match(parts, ["api", "tasks"])) {
    if (method === "GET") return json(await agent.listTasks());
    if (method === "POST") {
      const parsed = schemas.taskCreate.safeParse(body);
      if (!parsed.success) return error(`Invalid body: ${parsed.error.message}`);
      return json(await agent.createTask(parsed.data));
    }
    return error("Method not allowed", 405);
  }

  // /api/tasks/:id
  if (match(parts, ["api", "tasks", ":id"])) {
    if (method === "GET") {
      const task = await agent.getTask(parts[2]!);
      return task ? json(task) : error("Task not found", 404);
    }
    if (method === "PATCH") {
      const parsed = schemas.taskPatch.safeParse(body);
      if (!parsed.success) return error(`Invalid body: ${parsed.error.message}`);
      const task = await agent.updateTask(parts[2]!, parsed.data);
      return task ? json(task) : error("Task not found", 404);
    }
    return error("Method not allowed", 405);
  }

  // /api/projects
  if (match(parts, ["api", "projects"]) && method === "GET") {
    return json(await agent.listProjects());
  }

  // /api/pipeline — the 10-gate pipeline (closed loop from the dashboard).
  if (match(parts, ["api", "pipeline"]) && method === "GET") {
    return json({ gates: await agent.listPipelineGates() });
  }

  // /api/pipeline/ship — the Ship button: mark a plugin SHIPPED (Gate 10).
  if (match(parts, ["api", "pipeline", "ship"]) && method === "POST") {
    const parsed = schemas.ship.safeParse(body);
    if (!parsed.success) return error(`Invalid body: ${parsed.error.message}`);
    const r = await agent.shipPlugin(parsed.data.pluginId, {
      version: parsed.data.version,
      note: parsed.data.note,
    });
    return r.ok && r.product ? json(r.product) : error(r.error ?? "Ship failed", 404);
  }

  // /api/pipeline/gates/:id
  if (match(parts, ["api", "pipeline", "gates", ":id"])) {
    if (method === "PATCH") {
      const parsed = schemas.gatePatch.safeParse(body);
      if (!parsed.success) return error(`Invalid body: ${parsed.error.message}`);
      const r = await agent.updatePipelineGate(parts[3]!, parsed.data);
      return r.ok && r.gate ? json(r.gate) : error(r.error ?? "Gate not found", 404);
    }
    return error("Method not allowed", 405);
  }

  // /api/persons — onboarded people/roles (assignable task owners).
  if (match(parts, ["api", "persons"]) && method === "GET") {
    return json(await agent.listPersons());
  }

  // /api/finance
  if (match(parts, ["api", "finance"]) && method === "GET") {
    return json(await agent.getFinance());
  }
  if (match(parts, ["api", "finance", "snapshot"]) && method === "POST") {
    const parsed = schemas.snapshot.safeParse(body);
    if (!parsed.success) return error(`Invalid body: ${parsed.error.message}`);
    await agent.recordFinancialSnapshot(parsed.data);
    return json({ ok: true });
  }
  // /api/finance/metrics — record/update a headline product counter
  // (downloads / licensed seats). Keeps the Overview section accurate.
  if (match(parts, ["api", "finance", "metrics"]) && method === "POST") {
    const parsed = z
      .object({
        name: z.enum(["downloads", "licensed"]),
        value: z.number().nonnegative(),
        unit: z.string().max(32).optional(),
      })
      .safeParse(body);
    if (!parsed.success) return error(`Invalid body: ${parsed.error.message}`);
    return json(await agent.recordProductMetric(parsed.data));
  }

  // /api/risks
  if (match(parts, ["api", "risks"]) && method === "GET") {
    return json(await agent.listRisks());
  }

  // /api/approvals
  if (match(parts, ["api", "approvals"])) {
    if (method === "GET") {
      const status = urlSearch(request, "status");
      return json(await agent.listApprovals(status || undefined));
    }
    return error("Method not allowed", 405);
  }

  // /api/approvals/:id/approve | reject
  if (parts[0] === "api" && parts[1] === "approvals" && parts[3]) {
    const id = parts[2]!;
    const action = parts[3];
    const parsed = schemas.approve.safeParse(body);
    const note = parsed.success ? parsed.data.note : undefined;
    if (action === "approve" && method === "POST") {
      const r = await agent.approveApproval(id, note);
      return r.ok ? json(r.request) : error(r.error ?? "Failed", 404);
    }
    if (action === "reject" && method === "POST") {
      const r = await agent.rejectApproval(id, note);
      return r.ok ? json(r.request) : error(r.error ?? "Failed", 404);
    }
    return error("Method not allowed", 405);
  }

  // /api/memory
  if (match(parts, ["api", "memory"])) {
    if (method === "GET") {
      const layer = urlSearch(request, "layer");
      return json(await agent.listMemory(layer ? { layer } : undefined));
    }
    if (method === "POST") {
      const parsed = schemas.memory.safeParse(body);
      if (!parsed.success) return error(`Invalid body: ${parsed.error.message}`);
      return json(await agent.addMemory(parsed.data));
    }
    return error("Method not allowed", 405);
  }

  // /api/doctrine
  if (match(parts, ["api", "doctrine"])) {
    if (method === "GET") return json(await agent.listDoctrine());
    if (method === "POST" && urlSearch(request, "action") === "propose") {
      const parsed = schemas.doctrinePropose.safeParse(body);
      if (!parsed.success) return error(`Invalid body: ${parsed.error.message}`);
      return json(await agent.proposeDoctrine(parsed.data));
    }
    return error("Method not allowed", 405);
  }

  // /api/doctrine/:id/approve | reject
  if (parts[0] === "api" && parts[1] === "doctrine" && parts[3]) {
    const id = parts[2]!;
    const action = parts[3];
    if (action === "approve" && method === "POST") {
      const r = await agent.approveDoctrine(id);
      return r.ok ? json(r.active) : error(r.error ?? "Failed", 404);
    }
    if (action === "reject" && method === "POST") {
      const r = await agent.rejectDoctrine(id);
      return r.ok ? json({ ok: true }) : error(r.error ?? "Failed", 404);
    }
    return error("Method not allowed", 405);
  }

  // /api/activity
  if (match(parts, ["api", "activity"]) && method === "GET") {
    return json(await agent.listActivity());
  }

  // /api/products, /api/engineering, /api/marketing, /api/costs
  if (match(parts, ["api", "products"]) && method === "GET") return json(await agent.listProducts());
  if (match(parts, ["api", "engineering"]) && method === "GET") return json(await agent.listEngineeringIssues());
  if (match(parts, ["api", "marketing"]) && method === "GET") return json(await agent.listCampaigns());
  if (match(parts, ["api", "costs"]) && method === "GET") return json(await agent.listCosts());

  // /api/tools/execute
  if (match(parts, ["api", "tools", "execute"]) && method === "POST") {
    const parsed = schemas.tool.safeParse(body);
    if (!parsed.success) return error(`Invalid body: ${parsed.error.message}`);
    return json(await agent.executeTool(parsed.data.name, parsed.data.input));
  }

  // /api/tools/executions — recent tool runs (see the agent actually working)
  if (match(parts, ["api", "tools", "executions"]) && method === "GET") {
    return json(await agent.listToolExecutions());
  }

  // /api/agents/run — start a persistent long-horizon agent run
  if (match(parts, ["api", "agents", "run"]) && method === "POST") {
    const parsed = schemas.agentRun.safeParse(body);
    if (!parsed.success) return error(`Invalid body: ${parsed.error.message}`);
    return json(await agent.startAgentRun(parsed.data));
  }

  // /api/agents/runs — list recent long-horizon runs
  if (match(parts, ["api", "agents", "runs"]) && method === "GET") {
    return json(await agent.listAgentRuns());
  }

  // /api/agents/runs/:id — run detail
  if (match(parts, ["api", "agents", "runs", ":id"])) {
    if (method === "GET") {
      const run = await agent.getAgentRun(parts[3]!);
      return run ? json(run) : error("Run not found", 404);
    }
    return error("Method not allowed", 405);
  }

  // /api/agents/runs/:id/continue — resume a stopped/timed-out run
  if (match(parts, ["api", "agents", "runs", ":id", "continue"]) && method === "POST") {
    return json(await agent.continueAgentRun(parts[3]!));
  }

  // /api/local/tasks — create a task for the LOCAL machine (live web → local)
  if (match(parts, ["api", "local", "tasks"]) && method === "POST") {
    const parsed = schemas.localTaskCreate.safeParse(body);
    if (!parsed.success) return error(`Invalid body: ${parsed.error.message}`);
    return json(await agent.requestLocalTask({ prompt: parsed.data.prompt, taskId: parsed.data.taskId, source: "founder", meta: parsed.data.meta }));
  }

  // /api/local/tasks — list recent local tasks
  if (match(parts, ["api", "local", "tasks"]) && method === "GET") {
    return json(await agent.listLocalTasks());
  }

  // /api/local/tasks/claim — the local daemon claims the next PENDING task
  if (match(parts, ["api", "local", "tasks", "claim"]) && method === "POST") {
    const parsed = schemas.localTaskClaim.safeParse(body);
    if (!parsed.success) return error(`Invalid body: ${parsed.error.message}`);
    return json(await agent.claimLocalTask(parsed.data.claimer));
  }

  // /api/local/tasks/:id — detail (GET), heartbeat (POST RUNNING), or submit
  if (match(parts, ["api", "local", "tasks", ":id"])) {
    const id = parts[3]!;
    if (method === "GET") {
      const task = await agent.getLocalTask(id);
      return task ? json(task) : error("Local task not found", 404);
    }
    if (method === "POST") {
      const parsed = schemas.localTaskResult.safeParse(body);
      if (!parsed.success) return error(`Invalid body: ${parsed.error.message}`);
      // RUNNING = heartbeat from the daemon (extends the claim lease so the
      // task isn't reaped while the machine is still working on it).
      if (parsed.data.status === "RUNNING") {
        const task = await agent.heartbeatLocalTask(id);
        return task ? json(task) : error("Local task not found", 404);
      }
      const terminal = parsed.data as { status: "DONE" | "FAILED"; result?: string; error?: string };
      return json(await agent.submitLocalResult(id, terminal));
    }
    return error("Method not allowed", 405);
  }

  // /api/files — upload a file (live web → Executive can inspect it)
  if (match(parts, ["api", "files"]) && method === "POST") {
    const parsed = schemas.fileUpload.safeParse(body);
    if (!parsed.success) return error(`Invalid body: ${parsed.error.message}`);
    try {
      return json(await agent.uploadFile(parsed.data));
    } catch (err) {
      return error(err instanceof Error ? err.message : String(err), 400);
    }
  }

  // /api/files — list uploaded files (metadata only)
  if (match(parts, ["api", "files"]) && method === "GET") {
    return json(await agent.listUploadedFiles());
  }

  // /api/files/:id — get a file (full content) or delete it
  // NOTE: parts is ["api", "files", "<id>"] — the id lives at index 2.
  if (match(parts, ["api", "files", ":id"])) {
    const id = parts[2]!;
    if (method === "GET") {
      const file = await agent.getUploadedFile(id);
      return file ? json(file) : error("File not found", 404);
    }
    if (method === "DELETE") {
      return json(await agent.deleteUploadedFile(id));
    }
    return error("Method not allowed", 405);
  }

  // /api/health
  if (match(parts, ["api", "health"]) && method === "GET") {
    return json({ ok: true, name: "zarlino-executive-os", time: new Date().toISOString() });
  }

  // /api/calendar — list (GET) or create (POST) events. GET accepts from/to.
  if (match(parts, ["api", "calendar"]) && method === "GET") {
    return json(await agent.listCalendarEvents({ from: urlSearch(request, "from") ?? undefined, to: urlSearch(request, "to") ?? undefined }));
  }
  if (match(parts, ["api", "calendar"]) && method === "POST") {
    const parsed = z
      .object({
        title: z.string().min(1).max(200),
        startAt: z.string().min(8).max(40),
        endAt: z.string().min(8).max(40).optional(),
        allDay: z.boolean().optional(),
        location: z.string().max(200).optional(),
        description: z.string().max(2000).optional(),
      })
      .safeParse(body);
    if (!parsed.success) return error(`Invalid event body: ${parsed.error.message}`);
    try {
      return json(await agent.createCalendarEvent(parsed.data));
    } catch (err) {
      return error(err instanceof Error ? err.message : String(err), 400);
    }
  }
  // /api/calendar/:id — update (PATCH) or delete (DELETE)
  if (match(parts, ["api", "calendar", ":id"])) {
    const id = parts[2]!;
    if (method === "PATCH") {
      const parsed = z
        .object({
          title: z.string().min(1).max(200).optional(),
          startAt: z.string().min(8).max(40).optional(),
          endAt: z.string().min(8).max(40).optional(),
          status: z.enum(["SCHEDULED", "CANCELLED", "DONE"]).optional(),
          location: z.string().max(200).optional(),
        })
        .safeParse(body);
      if (!parsed.success) return error(`Invalid event body: ${parsed.error.message}`);
      try {
        return json(await agent.updateCalendarEvent(id, parsed.data));
      } catch (err) {
        return error(err instanceof Error ? err.message : String(err), 400);
      }
    }
    if (method === "DELETE") return json(await agent.deleteCalendarEvent(id));
    return error("Method not allowed", 405);
  }

  // /api/notes — list (GET) or create (POST)
  if (match(parts, ["api", "notes"]) && method === "GET") {
    return json(await agent.listNotes(Number(urlSearch(request, "limit") ?? 100) || 100));
  }
  if (match(parts, ["api", "notes"]) && method === "POST") {
    const parsed = z
      .object({
        title: z.string().min(1).max(200),
        body: z.string().max(10_000).optional(),
        tags: z.array(z.string().max(40)).max(10).optional(),
        pinned: z.boolean().optional(),
      })
      .safeParse(body);
    if (!parsed.success) return error(`Invalid note body: ${parsed.error.message}`);
    return json(await agent.createNote(parsed.data));
  }
  if (match(parts, ["api", "notes", ":id"])) {
    const id = parts[2]!;
    if (method === "DELETE") return json(await agent.deleteNote(id));
    return error("Method not allowed", 405);
  }

  // /api/reminders — list (GET; ?overdueOnly=1) or create (POST)
  if (match(parts, ["api", "reminders"]) && method === "GET") {
    return json(await agent.listReminders({ overdueOnly: urlSearch(request, "overdueOnly") === "1", limit: Number(urlSearch(request, "limit") ?? 100) || 100 }));
  }
  if (match(parts, ["api", "reminders"]) && method === "POST") {
    const parsed = z.object({ title: z.string().min(1).max(200), dueAt: z.string().min(8).max(40) }).safeParse(body);
    if (!parsed.success) return error(`Invalid reminder body: ${parsed.error.message}`);
    try {
      return json(await agent.createReminder(parsed.data));
    } catch (err) {
      return error(err instanceof Error ? err.message : String(err), 400);
    }
  }
  if (match(parts, ["api", "reminders", ":id"])) {
    const id = parts[2]!;
    if (method === "PATCH") {
      const parsed = z.object({ done: z.boolean().optional() }).safeParse(body);
      if (!parsed.success) return error(`Invalid reminder body: ${parsed.error.message}`);
      if (parsed.data.done === true) return json(await agent.markReminderDone(id));
      return json(await agent.listReminders({ limit: 1 }));
    }
    if (method === "DELETE") return json(await agent.deleteReminder(id));
    return error("Method not allowed", 405);
  }

  // /api/contacts — list (GET) or add (POST)
  if (match(parts, ["api", "contacts"]) && method === "GET") {
    return json(await agent.listContacts());
  }
  if (match(parts, ["api", "contacts"]) && method === "POST") {
    const parsed = z
      .object({
        name: z.string().min(1).max(200),
        role: z.string().max(200).optional(),
        company: z.string().max(200).optional(),
        email: z.string().max(300).optional(),
        phone: z.string().max(100).optional(),
        tags: z.array(z.string().max(40)).max(10).optional(),
        notes: z.string().max(2000).optional(),
      })
      .safeParse(body);
    if (!parsed.success) return error(`Invalid contact body: ${parsed.error.message}`);
    return json(await agent.addContact(parsed.data));
  }
  if (match(parts, ["api", "contacts", ":id"])) {
    const id = parts[2]!;
    if (method === "DELETE") return json(await agent.deleteContact(id));
    return error("Method not allowed", 405);
  }

  // /api/system/maintenance — run the self-healing sweep (reap stale runs,
  // re-queue expired local leases, purge expired sessions). Safe/idempotent.
  if (match(parts, ["api", "system", "maintenance"]) && method === "POST") {
    return json(await agent.runMaintenance());
  }

  // /api/system/budget — GET the effective AI budgets (+ spend/tokens) and
  // POST Founder overrides so the budget can be extended from the console.
  if (match(parts, ["api", "system", "budget"]) && method === "GET") {
    return json({ ok: true, ...(await agent.aiBudgetSnapshot()) });
  }
  if (match(parts, ["api", "system", "budget"]) && method === "POST") {
    const parsed = z
      .object({
        aiDailyBudget: z.number().positive().optional(),
        aiMonthlyBudget: z.number().positive().optional(),
        aiPerTaskBudget: z.number().positive().optional(),
        aiPerRunBudget: z.number().positive().optional(),
      })
      .safeParse(body);
    if (!parsed.success) return error(`Invalid budget body: ${parsed.error.message}`);
    const result = await agent.setAiBudget(parsed.data);
    if (!result.ok) return error(result.error ?? "Failed to update budget", 400);
    return json({ ok: true, view: result.view });
  }

  // /api/system/balance — AI-provider account balance (cached; refresh to
  // bypass the cache and hit the provider live).
  if (match(parts, ["api", "system", "balance", "refresh"]) && method === "POST") {
    return json(await agent.providerBalance(true));
  }
  if (match(parts, ["api", "system", "balance"]) && method === "GET") {
    return json(await agent.providerBalance(false));
  }

  // /api/system/snapshot — export (GET) a full portable JSON snapshot for
  // backup / restore. POST imports a snapshot back (full overwrite restore).
  if (match(parts, ["api", "system", "snapshot"]) && method === "GET") {
    return json(await agent.exportSnapshot());
  }
  if (match(parts, ["api", "system", "snapshot"]) && method === "POST") {
    const parsed = z.record(z.string(), z.array(z.unknown())).safeParse(body);
    if (!parsed.success) return error(`Invalid snapshot body: ${parsed.error.message}`);
    return json(await agent.importSnapshot(parsed.data));
  }

  // /api/system/health — deep health incl. stuck-work counts for ops.
  if (match(parts, ["api", "system", "health"]) && method === "GET") {
    const overview = (await agent.overview()) as Record<string, unknown>;
    const stuck = await agent.stuckCounts();
    return json({
      ok: true,
      name: "zarlino-executive-os",
      time: new Date().toISOString(),
      status: overview.status,
      paused: overview.paused,
      stuck,
      aiSpendToday: overview.aiSpendToday,
      aiDailyBudget: overview.aiDailyBudget,
      aiTokensToday: overview.aiTokensToday,
      aiDailyTokenBudget: overview.aiDailyTokenBudget,
      openTasks: overview.openTasks,
      pendingApprovals: overview.pendingApprovals,
    });
  }

  return error(`Not found: /${parts.join("/")}`, 404);
}

function match(parts: string[], pattern: string[]): boolean {
  if (parts.length !== pattern.length) return false;
  for (let i = 0; i < pattern.length; i++) {
    if (pattern[i] === ":id") continue;
    if (parts[i] !== pattern[i]) return false;
  }
  return true;
}

function urlSearch(request: Request, key: string): string | undefined {
  const value = new URL(request.url).searchParams.get(key);
  return value ?? undefined;
}
