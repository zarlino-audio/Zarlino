/**
 * Local Task Daemon — the live Executive OS's hands on the local machine.
 *
 * The live web console / Executive queues "local tasks" (e.g. "inspect the
 * ZScorch plugin UI and report what controls it has"). This daemon polls the
 * live worker, claims PENDING tasks, runs them through the local agent (which
 * can read repo files, inspect plugin UI source, run commands/tests/builds),
 * and posts the result back to the live worker so it appears in the web
 * console.
 *
 * Run:  npm run agent:daemon          (single pass: claim + run + report)
 *       npm run agent:daemon -- --loop (keep polling every N seconds)
 *       npm run agent:daemon -- --yes  (auto-confirm write/command/deploy tools)
 */

import { loadDotEnv } from "./dotenv.js";
import {
  API_KEY,
  AUTO_YES,
  buildSystem,
  ensureApiKey,
  EXEC_URL,
  REPO_ROOT,
  runTurn,
  TOKEN,
} from "./local-agent.js";

const dotenv = loadDotEnv("C:/Users/khrit/Desktop/plugin/executive-os/.dev.vars");
const FOUNDER_TOKEN = process.env.FOUNDER_AUTH_TOKEN ?? dotenv.FOUNDER_AUTH_TOKEN ?? TOKEN;
const CLAIMER = process.env.LOCAL_DAEMON_NAME ?? `local-${process.platform}-${Date.now() % 10000}`;
const LOOP = process.argv.includes("--loop");
const POLL_MS = Number(process.env.LOCAL_DAEMON_POLL_MS ?? 30_000);

if (!FOUNDER_TOKEN) {
  console.error("FOUNDER_AUTH_TOKEN not found — add it to executive-os/.dev.vars or the environment.");
  process.exit(1);
}

const headers = { Authorization: `Bearer ${FOUNDER_TOKEN}`, "Content-Type": "application/json" };

async function api(path: string, init?: RequestInit): Promise<unknown> {
  const res = await fetch(`${EXEC_URL}${path}`, { ...init, headers: { ...headers, ...(init?.headers ?? {}) } });
  if (!res.ok) throw new Error(`HTTP ${res.status}: ${(await res.text()).slice(0, 300)}`);
  return res.json();
}

/** Claim the next PENDING local task (or null). */
async function claimTask(): Promise<{ id: string; prompt: string } | null> {
  const task = (await api("/api/local/tasks/claim", {
    method: "POST",
    body: JSON.stringify({ claimer: CLAIMER }),
  })) as { id: string; prompt: string; status: string } | null;
  return task && task.id ? { id: task.id, prompt: task.prompt } : null;
}

/** Run one local task and post the result back. Returns true on success. */
async function processTask(task: { id: string; prompt: string }): Promise<boolean> {
  console.log(`\n\x1b[36m[daemon] claimed ${task.id}\x1b[0m — working locally…`);
  // Periodic heartbeat so the live worker never reaps our lease while we work.
  // (The server lease default is 10 min; long tasks can exceed that, so we
  // keep it alive every 60s.)
  let done = false;
  const hb = setInterval(async () => {
    if (done) return;
    await api(`/api/local/tasks/${task.id}`, { method: "POST", body: JSON.stringify({ status: "RUNNING" }) }).catch(() => null);
  }, 60_000);
  try {
    // Mark RUNNING immediately (also extends the lease).
    await api(`/api/local/tasks/${task.id}`, { method: "POST", body: JSON.stringify({ status: "RUNNING" }) }).catch(() => null);
    // Local work can need many tool calls (inspect files, run commands) — give
    // it a generous loop cap, bounded by the model + tool limits.
    const result = await runTurn(task.prompt, 30);
    done = true;
    clearInterval(hb);
    console.log(`\x1b[32m[daemon] finished ${task.id}\x1b[0m — ${result.length} chars`);
    await api(`/api/local/tasks/${task.id}`, {
      method: "POST",
      body: JSON.stringify({ status: "DONE", result }),
    });
    return true;
  } catch (err) {
    const message = err instanceof Error ? err.message : String(err);
    done = true;
    clearInterval(hb);
    console.error(`\x1b[31m[daemon] failed ${task.id}: ${message}\x1b[0m`);
    await api(`/api/local/tasks/${task.id}`, {
      method: "POST",
      body: JSON.stringify({ status: "FAILED", error: message }),
    }).catch(() => null);
    return false;
  }
}

async function main() {
  ensureApiKey();
  console.log(`\x1b[36mZarlino Local Task Daemon\x1b[0m — working on ${REPO_ROOT}`);
  console.log(`claiming as "${CLAIMER}" → ${EXEC_URL}${LOOP ? ` (polling every ${POLL_MS}ms)` : " (single pass)"}\n`);

  // Warm up: confirm we can reach the live worker + model.
  await buildSystem().catch(() => null);

  do {
    try {
      const task = await claimTask();
      if (task) {
        await processTask(task);
      } else if (!LOOP) {
        console.log("No pending local tasks. Done.");
        return;
      } else {
        process.stdout.write(".");
      }
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err);
      console.error(`\x1b[31m[daemon] error: ${message}\x1b[0m`);
      if (!LOOP) return;
    }
    if (LOOP) {
      await new Promise((r) => setTimeout(r, POLL_MS));
    }
  } while (LOOP);
}

const WATCHDOG = process.argv.includes("--watchdog");
if (WATCHDOG) {
  // Watchdog mode: if the daemon process dies or throws outside the loop
  // (network outage, crash), restart it automatically instead of leaving local
  // work stuck. Combined with the server-side lease reaper, no task is lost.
  const MAX_RESTARTS = 1000;
  let restarts = 0;
  (async function watchdog() {
    for (;;) {
      try {
        await main();
      } catch (err) {
        console.error(`[watchdog] daemon crashed: ${err instanceof Error ? err.message : err}`);
      }
      restarts += 1;
      if (restarts > MAX_RESTARTS) {
        console.error("[watchdog] giving up after too many restarts.");
        process.exit(1);
      }
      console.log(`[watchdog] restarting in 5s (restart #${restarts})…`);
      await new Promise((r) => setTimeout(r, 5000));
    }
  })();
} else {
  main().catch((err) => {
    console.error(err);
    process.exit(1);
  });
}
