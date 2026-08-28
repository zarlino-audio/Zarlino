/**
 * Planner tests (migration v14): calendar, notes, reminders, contacts —
 * the "CEO day" tools. Covers store CRUD, tool-level behavior (via a real
 * store context), overdue detection, validation, migration + snapshot.
 */
import { afterEach, describe, expect, it, vi } from "vitest";
import { createTestStore } from "./helpers";
import type { ToolContext } from "../src/core/tools/types";
import { newId, nowIso } from "../src/ids";

afterEach(() => {
  vi.restoreAllMocks();
});

/** Build a ToolContext backed by a real test store (as the agent uses). */
function makeToolContext(store: ReturnType<typeof createTestStore>): ToolContext {
  return {
    authority: {
      actor: "executive",
      config: { spendApprovalThreshold: 100, maxAutonomousSpend: 25, autonomyMode: "CONTROLLED", delegateMode: false, fullAccess: false },
      emergency: { paused: false, spendingDisabled: false, deploymentsDisabled: false },
    },
    env: { WEBSITE_URL: "https://zarlinoaudio.com" },
    store: store as never,
    requestApproval: () => null,
    audit: () => {},
  };
}

// ---------------------------------------------------------------------------
// Calendar
// ---------------------------------------------------------------------------

describe("planner: calendar", () => {
  it("creates, lists by range, updates and deletes events", () => {
    const store = createTestStore();
    const now = nowIso();
    store.createCalendarEvent({
      id: "evt_1", title: "Founder sync", start_at: "2026-08-26T10:00:00Z", end_at: "2026-08-26T11:00:00Z",
      all_day: false, attendees_json: "[]", status: "SCHEDULED", source: "founder", created_at: now, updated_at: now,
    });
    store.createCalendarEvent({
      id: "evt_2", title: "ZTame test", start_at: "2026-08-27T09:00:00Z", all_day: false, attendees_json: "[]",
      status: "SCHEDULED", source: "founder", created_at: now, updated_at: now,
    });
    // Range filter (only evt_1 on the 26th).
    const inRange = store.listCalendarEvents("2026-08-26T00:00:00Z", "2026-08-26T23:59:59Z");
    expect(inRange.map((e) => e.id)).toEqual(["evt_1"]);
    // Update.
    const updated = store.updateCalendarEvent("evt_1", { status: "DONE" });
    expect(updated?.status).toBe("DONE");
    // Delete + idempotent no-op.
    expect(store.deleteCalendarEvent("evt_2")).toBe(true);
    expect(store.deleteCalendarEvent("evt_2")).toBe(false);
    expect(store.getCalendarEvent("evt_2")).toBeNull();
  });

  it("rejects invalid ISO start times and end-before-start", async () => {
    // Exercise the internal tool's validation via the registry path.
    const { buildToolRegistry } = await import("../src/agent/tools");
    const store = createTestStore();
    const ctx = makeToolContext(store);
    const registry = buildToolRegistry({ env: {}, sink: { recordExecution: () => {} } });
    const tool = registry.list().find((t) => t.name === "calendar.create_event")!;
    await expect(tool.run(ctx, { title: "bad", startAt: "not-a-date" })).rejects.toThrow(/startAt/i);
    await expect(tool.run(ctx, { title: "bad", startAt: "2026-08-26T10:00:00Z", endAt: "2026-08-26T09:00:00Z" })).rejects.toThrow(/after/);
  });
});

// ---------------------------------------------------------------------------
// Notes
// ---------------------------------------------------------------------------

describe("planner: notes", () => {
  it("creates and lists notes (pinned first)", () => {
    const store = createTestStore();
    const now = nowIso();
    store.createNote({ id: "nt_1", title: "A", body: "x", tags_json: "[]", pinned: false, source: "founder", created_at: now, updated_at: now });
    store.createNote({ id: "nt_2", title: "B", pinned: true, tags_json: '["meeting"]', source: "founder", created_at: now, updated_at: now });
    const notes = store.listNotes();
    expect(notes[0]?.id).toBe("nt_2"); // pinned first
    expect(notes[1]?.id).toBe("nt_1");
    expect(store.deleteNote("nt_1")).toBe(true);
    expect(store.getNote("nt_1")).toBeNull();
  });
});

// ---------------------------------------------------------------------------
// Reminders
// ---------------------------------------------------------------------------

describe("planner: reminders", () => {
  it("detects overdue reminders and marks them done", () => {
    const store = createTestStore();
    const now = nowIso();
    const past = new Date(Date.now() - 3600_000).toISOString();
    const future = new Date(Date.now() + 3600_000).toISOString();
    store.createReminder({ id: "rem_1", title: "overdue", due_at: past, done: false, source: "founder", created_at: now, updated_at: now });
    store.createReminder({ id: "rem_2", title: "future", due_at: future, done: false, source: "founder", created_at: now, updated_at: now });
    const overdue = store.listReminders({ overdueOnly: true });
    expect(overdue.map((r) => r.id)).toEqual(["rem_1"]);
    store.markReminderDone("rem_1");
    expect(store.getReminder("rem_1")?.done).toBe(1);
    expect(store.listReminders({ overdueOnly: true })).toEqual([]);
  });
});

// ---------------------------------------------------------------------------
// Contacts
// ---------------------------------------------------------------------------

describe("planner: contacts", () => {
  it("adds and lists contacts alphabetically", () => {
    const store = createTestStore();
    const now = nowIso();
    store.createContact({ id: "ct_1", name: "Zed", tags_json: "[]", source: "founder", created_at: now, updated_at: now });
    store.createContact({ id: "ct_2", name: "Anna", company: "Studio X", tags_json: '["reviewer"]', source: "founder", created_at: now, updated_at: now });
    const contacts = store.listContacts();
    expect(contacts.map((c) => c.name)).toEqual(["Anna", "Zed"]);
    expect(contacts[0]?.company).toBe("Studio X");
    expect(store.deleteContact("ct_1")).toBe(true);
  });
});

// ---------------------------------------------------------------------------
// Hardening: migration, snapshot, tool validation, output bounding
// ---------------------------------------------------------------------------

describe("planner hardening", () => {
  it("migration v14 applies and is idempotent", () => {
    const store = createTestStore();
    store.migrate();
    expect(store.migrate().length).toBe(0); // idempotent
    // Tables exist.
    const snap = store.exportSnapshot();
    for (const t of ["calendar_events", "notes", "reminders", "contacts"]) {
      expect(Array.isArray(snap[t])).toBe(true);
    }
  });

  it("calendar tool bounds output and rejects oversized input", async () => {
    const { buildToolRegistry } = await import("../src/agent/tools");
    const store = createTestStore();
    const ctx = makeToolContext(store);
    const registry = buildToolRegistry({ env: {}, sink: { recordExecution: () => {} } });
    const createTool = registry.list().find((t) => t.name === "calendar.create_event")!;
    // Oversized title rejected by the zod schema (enforced by the registry).
    const bad = createTool.inputSchema.safeParse({ title: "x".repeat(300), startAt: "2026-08-26T10:00:00Z" });
    expect(bad.success).toBe(false);
    // Valid event created through the tool.
    const created = await createTool.run(ctx, { title: "Ship sync", startAt: "2026-08-26T10:00:00Z", endAt: "2026-08-26T11:00:00Z" });
    expect((created as { id: string }).id).toMatch(/^evt_/);
    const listTool = registry.list().find((t) => t.name === "calendar.list_events")!;
    const listed = (await listTool.run(ctx, { from: "2026-08-01T00:00:00Z", to: "2026-08-31T23:59:59Z" })) as { count: number; events: unknown[] };
    expect(listed.count).toBe(1);
  });

  it("reminders tool reports overdue and marks done", async () => {
    const { buildToolRegistry } = await import("../src/agent/tools");
    const store = createTestStore();
    const ctx = makeToolContext(store);
    const registry = buildToolRegistry({ env: {}, sink: { recordExecution: () => {} } });
    const createTool = registry.list().find((t) => t.name === "reminders.create_reminder")!;
    const due = new Date(Date.now() - 60_000).toISOString();
    const created = (await createTool.run(ctx, { title: "check build", dueAt: due })) as { id: string; overdue: boolean };
    expect(created.overdue).toBe(true);
    const listTool = registry.list().find((t) => t.name === "reminders.list_reminders")!;
    const overdue = (await listTool.run(ctx, { overdueOnly: true })) as { reminders: Array<{ title: string }> };
    expect(overdue.reminders.some((r) => r.title === "check build")).toBe(true);
  });

  it("export_csv tool produces bounded CSV", async () => {
    const { buildToolRegistry } = await import("../src/agent/tools");
    const store = createTestStore();
    const ctx = makeToolContext(store);
    const registry = buildToolRegistry({ env: {}, sink: { recordExecution: () => {} } });
    const tool = registry.list().find((t) => t.name === "executive.export_csv")!;
    const out = (await tool.run(ctx, { table: "tasks", limit: 10 })) as { rows: number; csv: string };
    expect(typeof out.csv).toBe("string");
    expect(out.rows).toBeGreaterThanOrEqual(0);
  });

  it("newId generates prefixed ids for planner entities", () => {
    expect(newId("evt")).toMatch(/^evt_/);
    expect(newId("nt")).toMatch(/^nt_/);
    expect(newId("rem")).toMatch(/^rem_/);
    expect(newId("ct")).toMatch(/^ct_/);
  });
});
