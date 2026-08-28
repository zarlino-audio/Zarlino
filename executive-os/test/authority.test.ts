import { describe, expect, it } from "vitest";
import { evaluateAction, defaultActions, defaultAuthorityConfig, defaultEmergency } from "../src/core/authority/engine";
import type { ActionSpec, AuthorityContext } from "../src/core/authority/engine";

function ctx(overrides: Partial<AuthorityContext> = {}): AuthorityContext {
  return {
    actor: "executive",
    config: defaultAuthorityConfig(),
    emergency: defaultEmergency(),
    ...overrides,
  };
}

const spec = (p: Partial<ActionSpec>): ActionSpec => ({
  name: "test.action",
  permission: "execute",
  description: "test",
  riskLevel: "LOW",
  reversible: true,
  auditRequired: true,
  ...p,
});

describe("authority engine (spec §11, §41)", () => {
  it("allows read/analyze/recommend autonomously", () => {
    for (const permission of ["read", "analyze", "recommend"] as const) {
      const d = evaluateAction(spec({ permission }), ctx());
      expect(d.verdict).toBe("ALLOW");
    }
  });

  it("allows low-risk reversible create/execute", () => {
    const d = evaluateAction(spec({ permission: "execute", riskLevel: "LOW", reversible: true }), ctx());
    expect(d.verdict).toBe("ALLOW");
  });

  it("escalates medium-risk execute to approval", () => {
    const d = evaluateAction(spec({ permission: "execute", riskLevel: "MEDIUM" }), ctx());
    expect(d.verdict).toBe("APPROVAL");
    expect(d.approvalRequired).toBe(true);
  });

  it("denies high-risk irreversible execute without founder", () => {
    const d = evaluateAction(spec({ permission: "execute", riskLevel: "HIGH", reversible: false }), ctx());
    expect(d.verdict).toBe("APPROVAL");
  });

  it("never allows delete or ownership transfer autonomously", () => {
    for (const permission of ["delete", "ownership_transfer"] as const) {
      const d = evaluateAction(spec({ permission }), ctx({ actor: "founder" }));
      expect(d.verdict).toBe("DENY");
    }
  });

  it("allows spend within autonomous threshold", () => {
    const d = evaluateAction(spec({ permission: "spend", costEstimate: 10, riskLevel: "LOW" }), ctx());
    expect(d.verdict).toBe("ALLOW");
  });

  it("requires approval for spend above autonomous threshold", () => {
    const d = evaluateAction(spec({ permission: "spend", costEstimate: 50, riskLevel: "MEDIUM" }), ctx());
    expect(d.verdict).toBe("APPROVAL");
  });

  it("requires approval for spend above approval threshold", () => {
    const d = evaluateAction(spec({ permission: "spend", costEstimate: 250 }), ctx());
    expect(d.verdict).toBe("APPROVAL");
  });

  it("denies spend when spending is disabled (emergency)", () => {
    const d = evaluateAction(
      spec({ permission: "spend", costEstimate: 10 }),
      ctx({ emergency: { ...defaultEmergency(), spendingDisabled: true } }),
    );
    expect(d.verdict).toBe("DENY");
  });

  it("denies deploy when deployments are disabled", () => {
    const d = evaluateAction(
      spec({ permission: "deploy", riskLevel: "HIGH" }),
      ctx({ emergency: { ...defaultEmergency(), deploymentsDisabled: true } }),
    );
    expect(d.verdict).toBe("DENY");
  });

  it("requires approval for production deployment", () => {
    const d = evaluateAction(spec({ permission: "deploy", riskLevel: "HIGH" }), ctx());
    expect(d.verdict).toBe("APPROVAL");
  });

  it("denies everything except reads when paused (emergency)", () => {
    const paused = ctx({ emergency: { ...defaultEmergency(), paused: true } });
    expect(evaluateAction(spec({ permission: "read" }), paused).verdict).toBe("ALLOW");
    expect(evaluateAction(spec({ permission: "create" }), paused).verdict).toBe("DENY");
    expect(evaluateAction(spec({ permission: "execute" }), paused).verdict).toBe("DENY");
  });

  it("requires approval for doctrine changes", () => {
    const d = evaluateAction(spec({ permission: "doctrine_change", riskLevel: "HIGH" }), ctx());
    expect(d.verdict).toBe("APPROVAL");
  });

  it("denies critical-risk actions even for the founder actor path", () => {
    const d = evaluateAction(spec({ permission: "execute", riskLevel: "CRITICAL" }), ctx({ actor: "founder" }));
    expect(d.verdict).toBe("DENY");
  });

  it("approval_only mode escalates all non-read actions", () => {
    const cfg = { ...defaultAuthorityConfig(), autonomyMode: "APPROVAL_ONLY" as const };
    expect(evaluateAction(spec({ permission: "create" }), ctx({ config: cfg })).verdict).toBe("APPROVAL");
    expect(evaluateAction(spec({ permission: "read" }), ctx({ config: cfg })).verdict).toBe("ALLOW");
  });

  it("full access overrides a stale approval_only mode (tools aren't blocked)", () => {
    const cfg = { ...defaultAuthorityConfig(), autonomyMode: "APPROVAL_ONLY" as const, fullAccess: true };
    expect(evaluateAction(spec({ permission: "create" }), ctx({ config: cfg })).verdict).toBe("ALLOW");
    expect(evaluateAction(spec({ permission: "execute", riskLevel: "MEDIUM" }), ctx({ config: cfg })).verdict).toBe("ALLOW");
    expect(evaluateAction(spec({ permission: "read" }), ctx({ config: cfg })).verdict).toBe("ALLOW");
  });

  it("delegate mode overrides a stale approval_only mode for its scope", () => {
    const cfg = { ...defaultAuthorityConfig(), autonomyMode: "APPROVAL_ONLY" as const, delegateMode: true };
    expect(evaluateAction(spec({ permission: "create" }), ctx({ config: cfg })).verdict).toBe("ALLOW");
    expect(evaluateAction(spec({ permission: "execute", riskLevel: "MEDIUM", reversible: true }), ctx({ config: cfg })).verdict).toBe("ALLOW");
    // HIGH risk still gates.
    expect(evaluateAction(spec({ permission: "execute", riskLevel: "HIGH" }), ctx({ config: cfg })).verdict).toBe("APPROVAL");
  });

  it("delegate mode allows production deployment without approval", () => {
    const cfg = { ...defaultAuthorityConfig(), delegateMode: true };
    const d = evaluateAction(spec({ permission: "deploy", riskLevel: "HIGH" }), ctx({ config: cfg }));
    expect(d.verdict).toBe("ALLOW");
  });

  it("delegate mode allows medium-risk reversible execute without approval", () => {
    const cfg = { ...defaultAuthorityConfig(), delegateMode: true };
    const d = evaluateAction(spec({ permission: "execute", riskLevel: "MEDIUM", reversible: true }), ctx({ config: cfg }));
    expect(d.verdict).toBe("ALLOW");
  });

  it("delegate mode still gates irreversible medium-risk actions", () => {
    const cfg = { ...defaultAuthorityConfig(), delegateMode: true };
    const d = evaluateAction(spec({ permission: "execute", riskLevel: "MEDIUM", reversible: false }), ctx({ config: cfg }));
    expect(d.verdict).toBe("APPROVAL");
  });

  it("delegate mode never bypasses emergency switches or hard gates", () => {
    const cfg = { ...defaultAuthorityConfig(), delegateMode: true };
    // deployments disabled → still DENY even in delegate mode.
    const dep = evaluateAction(
      spec({ permission: "deploy", riskLevel: "HIGH" }),
      ctx({ config: cfg, emergency: { ...defaultEmergency(), deploymentsDisabled: true } }),
    );
    expect(dep.verdict).toBe("DENY");
    // HIGH risk execute → still APPROVAL (delegate only covers MEDIUM + deploy).
    expect(evaluateAction(spec({ permission: "execute", riskLevel: "HIGH" }), ctx({ config: cfg })).verdict).toBe("APPROVAL");
    // CRITICAL → still DENY.
    expect(evaluateAction(spec({ permission: "execute", riskLevel: "CRITICAL" }), ctx({ config: cfg })).verdict).toBe("DENY");
    // PAUSED autonomy mode → still DENY for non-read.
    const pausedCfg = { ...cfg, autonomyMode: "PAUSED" as const };
    expect(evaluateAction(spec({ permission: "deploy", riskLevel: "HIGH" }), ctx({ config: pausedCfg })).verdict).toBe("DENY");
  });

  it("full access allows deploy, HIGH-risk reversible, and spend autonomously", () => {
    const cfg = { ...defaultAuthorityConfig(), fullAccess: true };
    expect(evaluateAction(spec({ permission: "deploy", riskLevel: "HIGH" }), ctx({ config: cfg })).verdict).toBe("ALLOW");
    expect(evaluateAction(spec({ permission: "execute", riskLevel: "HIGH", reversible: true }), ctx({ config: cfg })).verdict).toBe("ALLOW");
    expect(evaluateAction(spec({ permission: "spend", costEstimate: 9999, riskLevel: "HIGH" }), ctx({ config: cfg })).verdict).toBe("ALLOW");
  });

  it("full access still enforces the hard NEVER-gates", () => {
    const cfg = { ...defaultAuthorityConfig(), fullAccess: true };
    // CRITICAL never autonomous.
    expect(evaluateAction(spec({ permission: "execute", riskLevel: "CRITICAL" }), ctx({ config: cfg })).verdict).toBe("DENY");
    // delete / ownership transfer never autonomous.
    expect(evaluateAction(spec({ permission: "delete" }), ctx({ config: cfg })).verdict).toBe("DENY");
    expect(evaluateAction(spec({ permission: "ownership_transfer" }), ctx({ config: cfg })).verdict).toBe("DENY");
    // Doctrine changes remain founder-approved.
    expect(evaluateAction(spec({ permission: "doctrine_change", riskLevel: "HIGH" }), ctx({ config: cfg })).verdict).toBe("APPROVAL");
    // Irreversible HIGH still approval.
    expect(evaluateAction(spec({ permission: "execute", riskLevel: "HIGH", reversible: false }), ctx({ config: cfg })).verdict).toBe("APPROVAL");
    // Emergency switches still override.
    const dep = evaluateAction(
      spec({ permission: "deploy", riskLevel: "HIGH" }),
      ctx({ config: cfg, emergency: { ...defaultEmergency(), deploymentsDisabled: true } }),
    );
    expect(dep.verdict).toBe("DENY");
    const spend = evaluateAction(
      spec({ permission: "spend", costEstimate: 10 }),
      ctx({ config: cfg, emergency: { ...defaultEmergency(), spendingDisabled: true } }),
    );
    expect(spend.verdict).toBe("DENY");
  });

  it("exposes the standard action set with all metadata (spec §13)", () => {
    const actions = defaultActions();
    expect(actions.length).toBeGreaterThan(20);
    for (const a of actions) {
      expect(a.name).toBeTruthy();
      expect(a.description).toBeTruthy();
      expect(a.permission).toBeTruthy();
      expect(a.riskLevel).toBeTruthy();
      expect(typeof a.reversible).toBe("boolean");
      expect(typeof a.auditRequired).toBe("boolean");
    }
    const names = new Set(actions.map((a) => a.name));
    expect(names.size).toBe(actions.length);
  });
});
