import { describe, expect, it } from "vitest";
import { canTransition, createApprovalRequest, transitionApproval } from "../src/core/approvals/queue";

describe("approval workflow (spec §12)", () => {
  it("creates a PENDING approval request", () => {
    const request = createApprovalRequest({
      action_label: "Spend $250 on advertising",
      reason: "Validate affiliate acquisition funnel",
      expected_upside: "First paying affiliate customer",
      max_downside: "$250",
      budget_available: "$500",
    });
    expect(request.status).toBe("PENDING");
    expect(request.recommendation).toBe("APPROVE");
    expect(request.id).toBeTruthy();
  });

  it("approves a PENDING request and records who decided", () => {
    const request = createApprovalRequest({ action_label: "X", reason: "r" });
    const result = transitionApproval(request, "approve", "founder", "ok");
    expect(result.ok).toBe(true);
    expect(result.request?.status).toBe("APPROVED");
    expect(result.request?.decided_by).toBe("founder");
    expect(result.request?.decided_at).toBeTruthy();
  });

  it("rejects and cancels", () => {
    const a = createApprovalRequest({ action_label: "X", reason: "r" });
    expect(transitionApproval(a, "reject", "founder").request?.status).toBe("REJECTED");
    const b = createApprovalRequest({ action_label: "Y", reason: "r" });
    expect(transitionApproval(b, "cancel", "executive").request?.status).toBe("CANCELLED");
  });

  it("allows modify on PENDING only", () => {
    const a = createApprovalRequest({ action_label: "X", reason: "r" });
    const modified = transitionApproval(a, "modify", "founder");
    expect(modified.ok).toBe(true);
    expect(modified.request?.status).toBe("MODIFIED");
    expect(canTransition("MODIFIED", "modify")).toContain("Cannot modify");
  });

  it("rejects invalid transitions", () => {
    const approved = transitionApproval(
      createApprovalRequest({ action_label: "X", reason: "r" }),
      "approve",
      "founder",
    ).request!;
    const again = transitionApproval(approved, "approve", "founder");
    expect(again.ok).toBe(false);
    expect(again.error).toBeTruthy();
  });

  it("cannot approve a REJECTED request", () => {
    const rejected = transitionApproval(
      createApprovalRequest({ action_label: "X", reason: "r" }),
      "reject",
      "founder",
    ).request!;
    expect(canTransition("REJECTED", "approve")).toBeTruthy();
    expect(transitionApproval(rejected, "approve", "founder").ok).toBe(false);
  });
});
