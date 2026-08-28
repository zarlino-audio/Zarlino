/**
 * Authority system (spec §11).
 *
 * A real permission engine enforced in backend code — prompt instructions are
 * NOT security controls. Every tool/action is tagged with a permission level;
 * the engine decides ALLOW / APPROVAL / DENY based on the actor, configured
 * thresholds, risk, reversibility, and emergency switches.
 */

export type Permission =
  | "read"
  | "analyze"
  | "recommend"
  | "create"
  | "modify"
  | "execute"
  | "spend"
  | "deploy"
  | "delete"
  | "approve"
  | "doctrine_change"
  | "ownership_transfer";

export type Actor = "founder" | "executive" | "system";

export type Verdict = "ALLOW" | "APPROVAL" | "DENY";

export type RiskLevel = "LOW" | "MEDIUM" | "HIGH" | "CRITICAL";

export interface ActionSpec {
  /** Tool/action name, e.g. "github.create_issue". */
  name: string;
  permission: Permission;
  description: string;
  riskLevel: RiskLevel;
  /** Estimated cost in USD when the action costs money. */
  costEstimate?: number;
  reversible: boolean;
  auditRequired: boolean;
}

export interface AuthorityConfig {
  spendApprovalThreshold: number;
  maxAutonomousSpend: number;
  autonomyMode: "CONTROLLED" | "APPROVAL_ONLY" | "PAUSED";
  /**
   * Delegate mode (Founder-granted). When ON, the Executive may autonomously
   * execute real work: deployments and MEDIUM-risk reversible modify/execute
   * actions run without per-action approval. HIGH/CRITICAL risk, irreversible
   * actions, and every emergency switch still gate regardless.
   */
  delegateMode: boolean;
  /**
   * Full access (Founder-granted, implies delegate). When ON, the Executive
   * runs virtually everything autonomously: deployments, HIGH-risk reversible
   * actions, and spending all ALLOW (spending still respects the spending-
   * disabled emergency switch). The hard NEVER-gates still hold: delete,
   * ownership transfer, doctrine changes, CRITICAL risk, irreversible HIGH,
   * and every emergency switch.
   */
  fullAccess: boolean;
}

export interface EmergencyState {
  paused: boolean;
  spendingDisabled: boolean;
  deploymentsDisabled: boolean;
}

export interface AuthorityContext {
  actor: Actor;
  config: AuthorityConfig;
  emergency: EmergencyState;
}

export interface AuthorityDecision {
  verdict: Verdict;
  requiredPermission: Permission;
  reasons: string[];
  /** When APPROVAL, an approval request should be created. */
  approvalRequired: boolean;
}

/** Baseline autonomy policy (spec §41). Maps permission → default verdict. */
const BASE_POLICY: Record<Permission, Verdict> = {
  read: "ALLOW",
  analyze: "ALLOW",
  recommend: "ALLOW",
  create: "ALLOW",
  modify: "ALLOW",
  execute: "ALLOW",
  spend: "ALLOW", // further constrained by thresholds below
  deploy: "APPROVAL",
  delete: "DENY", // NEVER autonomous
  approve: "APPROVAL", // only the Founder approves
  doctrine_change: "APPROVAL",
  ownership_transfer: "DENY", // NEVER autonomous
};

/**
 * Evaluate an action against the authority policy.
 *
 * Order of checks:
 *   1. Emergency pause → deny everything except pure reads.
 *   2. Autonomy mode APPROVAL_ONLY → non-read actions become APPROVAL.
 *   3. NEVER-autonomous permissions → DENY.
 *   4. Risk: HIGH/CRITICAL risk requires approval; CRITICAL never autonomous.
 *   5. Permission-specific threshold checks (spend, deploy, modify).
 *   6. Reversibility: irreversible actions escalate to approval.
 */
export function evaluateAction(
  spec: ActionSpec,
  ctx: AuthorityContext,
): AuthorityDecision {
  const reasons: string[] = [];

  // 1. Emergency pause (spec §39) — enforced server-side.
  if (ctx.emergency.paused && spec.permission !== "read") {
    return {
      verdict: "DENY",
      requiredPermission: spec.permission,
      reasons: ["Emergency pause is active — autonomous execution is suspended."],
      approvalRequired: false,
    };
  }

  // 2. Approval-only mode. Full access (or delegate, for its scope) is a
  // stronger Founder grant — it overrides a stale APPROVAL_ONLY mode so the
  // Executive isn't blocked from the work the Founder explicitly unlocked.
  if (ctx.config.autonomyMode === "PAUSED" && spec.permission !== "read") {
    return {
      verdict: "DENY",
      requiredPermission: spec.permission,
      reasons: ["Autonomy mode is PAUSED."],
      approvalRequired: false,
    };
  }
  if (ctx.config.autonomyMode === "APPROVAL_ONLY" && spec.permission !== "read" && !ctx.config.fullAccess && !ctx.config.delegateMode) {
    return {
      verdict: "APPROVAL",
      requiredPermission: spec.permission,
      reasons: ["Autonomy mode is APPROVAL_ONLY."],
      approvalRequired: true,
    };
  }

  // 3. NEVER-autonomous permissions.
  const base = BASE_POLICY[spec.permission];
  if (base === "DENY") {
    return {
      verdict: "DENY",
      requiredPermission: spec.permission,
      reasons: [`Permission "${spec.permission}" is never autonomous (${spec.name}).`],
      approvalRequired: false,
    };
  }

  // 3.5. Permission-specific emergency + threshold checks FIRST, so an
  // emergency switch (e.g. deployments disabled) can never be masked by the
  // generic risk gate below.
  switch (spec.permission) {
    case "spend": {
      if (ctx.emergency.spendingDisabled) {
        return {
          verdict: "DENY",
          requiredPermission: "spend",
          reasons: ["Spending is disabled by the Founder."],
          approvalRequired: false,
        };
      }
      if (ctx.config.fullAccess) {
        reasons.push("Full access mode — spending is autonomous.");
        return { verdict: "ALLOW", requiredPermission: "spend", reasons, approvalRequired: false };
      }
      const cost = spec.costEstimate ?? 0;
      if (cost <= ctx.config.maxAutonomousSpend && spec.riskLevel === "LOW") {
        reasons.push(`Spend ${cost} within autonomous threshold (≤ ${ctx.config.maxAutonomousSpend}).`);
        return { verdict: "ALLOW", requiredPermission: "spend", reasons, approvalRequired: false };
      }
      if (cost <= ctx.config.spendApprovalThreshold) {
        reasons.push(`Spend ${cost} within approval threshold (≤ ${ctx.config.spendApprovalThreshold}).`);
        return { verdict: "APPROVAL", requiredPermission: "spend", reasons, approvalRequired: true };
      }
      return {
        verdict: "APPROVAL",
        requiredPermission: "spend",
        reasons: [`Spend ${cost} exceeds approval threshold — Founder decision required.`],
        approvalRequired: true,
      };
    }
    case "deploy": {
      if (ctx.emergency.deploymentsDisabled) {
        return {
          verdict: "DENY",
          requiredPermission: "deploy",
          reasons: ["Deployments are disabled by the Founder."],
          approvalRequired: false,
        };
      }
      if (ctx.config.delegateMode || ctx.config.fullAccess) {
        reasons.push("Delegate/full-access mode enabled — deployment allowed.");
        return { verdict: "ALLOW", requiredPermission: "deploy", reasons, approvalRequired: false };
      }
      reasons.push("Production deployment requires Founder approval.");
      return { verdict: "APPROVAL", requiredPermission: "deploy", reasons, approvalRequired: true };
    }
    default:
      break;
  }

  // 4. Risk gating (spec §23): HIGH requires approval (unless full-access and
  // reversible), CRITICAL never autonomous.
  if (spec.riskLevel === "CRITICAL") {
    return {
      verdict: "DENY",
      requiredPermission: spec.permission,
      reasons: ["Action is CRITICAL risk — never autonomous."],
      approvalRequired: false,
    };
  }
  if (spec.riskLevel === "HIGH" && ctx.actor !== "founder") {
    if (ctx.config.fullAccess && spec.reversible) {
      reasons.push("Full access mode — HIGH-risk reversible action allowed.");
    } else {
      return {
        verdict: "APPROVAL",
        requiredPermission: spec.permission,
        reasons: ["Action is HIGH risk — requires approval."],
        approvalRequired: true,
      };
    }
  }

  // 5. Reversibility / medium-risk escalation for modify & execute.
  switch (spec.permission) {
    case "modify":
    case "execute": {
      if (!spec.reversible && spec.riskLevel !== "LOW") {
        reasons.push("Action is not reversible and risk is above LOW.");
        return { verdict: "APPROVAL", requiredPermission: spec.permission, reasons, approvalRequired: true };
      }
      if (spec.riskLevel === "MEDIUM") {
        if (ctx.config.delegateMode || ctx.config.fullAccess) {
          reasons.push("Delegate/full-access mode enabled — MEDIUM-risk action allowed.");
        } else {
          reasons.push("Action is MEDIUM risk — requires approval.");
          return { verdict: "APPROVAL", requiredPermission: spec.permission, reasons, approvalRequired: true };
        }
      }
      break;
    }
    default:
      break;
  }

  reasons.push(`Baseline policy for "${spec.permission}" is ${base}.`);
  return { verdict: base, requiredPermission: spec.permission, reasons, approvalRequired: base === "APPROVAL" };
}

/** Build the standard default action set (spec §13 examples). */
export function defaultActions(): ActionSpec[] {
  return [
    { name: "github.read_repo", permission: "read", description: "Read a GitHub repository", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "github.search", permission: "analyze", description: "Search GitHub", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "github.create_issue", permission: "create", description: "Create a GitHub issue", riskLevel: "LOW", reversible: true, auditRequired: true },
    { name: "github.update_issue", permission: "modify", description: "Update issue status", riskLevel: "LOW", reversible: true, auditRequired: true },
    { name: "github.list_workflows", permission: "read", description: "List GitHub Actions workflows", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "github.trigger_workflow", permission: "execute", description: "Trigger a GitHub Actions workflow (CI/release)", riskLevel: "MEDIUM", reversible: true, auditRequired: true },
    { name: "github.list_branches", permission: "read", description: "List repository branches", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "github.write_file", permission: "modify", description: "Write/commit a file to the repository", riskLevel: "MEDIUM", reversible: true, auditRequired: true },
    { name: "github.create_pull_request", permission: "execute", description: "Open a pull request", riskLevel: "LOW", reversible: true, auditRequired: true },
    { name: "github.merge_pull_request", permission: "execute", description: "Merge a pull request", riskLevel: "MEDIUM", reversible: false, auditRequired: true },
    { name: "github.list_workflow_runs", permission: "read", description: "List GitHub Actions runs", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "github.get_workflow_run", permission: "read", description: "Get a GitHub Actions run", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "github.create_branch", permission: "execute", description: "Create a branch", riskLevel: "LOW", reversible: true, auditRequired: true },
    { name: "github.merge_pr", permission: "execute", description: "Merge a pull request", riskLevel: "MEDIUM", reversible: false, auditRequired: true },
    { name: "github.list_releases", permission: "read", description: "List repository releases", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "github.get_file", permission: "read", description: "Read a file from the repository", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "github.list_commits", permission: "read", description: "List repository commits", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "github.list_repos", permission: "read", description: "List organization repositories", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "github.get_website_source", permission: "read", description: "Read website source file", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "github.list_collaborators", permission: "read", description: "List repo collaborators", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "github.get_commit_diff", permission: "read", description: "Get a commit diff", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "github.get_file_metadata", permission: "read", description: "Get file metadata", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "cloudflare.list_workers", permission: "read", description: "List Worker scripts", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "cloudflare.get_worker", permission: "read", description: "Read Worker config", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "cloudflare.get_worker_source", permission: "read", description: "Download Worker source", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "cloudflare.get_deployment", permission: "read", description: "Read deployment info", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "cloudflare.get_analytics", permission: "analyze", description: "Read analytics", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "cloudflare.deploy_worker", permission: "deploy", description: "Deploy a Worker", riskLevel: "HIGH", reversible: false, auditRequired: true },
    { name: "cloudflare.list_zones", permission: "read", description: "List zones/domains", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "cloudflare.get_zone", permission: "read", description: "Read a zone by name", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "cloudflare.get_dns_records", permission: "read", description: "List DNS records", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "cloudflare.get_account", permission: "read", description: "Read account profile", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "vision.analyze_image", permission: "read", description: "Analyze an image with vision", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "vision.analyze_url_image", permission: "read", description: "Analyze an image from a URL", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "web.search", permission: "read", description: "Search the web", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "web.fetch_page", permission: "read", description: "Fetch a web page", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "web.check_url", permission: "read", description: "Check a URL", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "web.crawl_links", permission: "read", description: "Extract links from a page", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "finance.get_snapshot", permission: "read", description: "Read financial snapshot", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "finance.calculate_runway", permission: "analyze", description: "Calculate runway", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "finance.record_expense", permission: "modify", description: "Record an expense", riskLevel: "LOW", reversible: true, auditRequired: true },
    { name: "finance.record_revenue", permission: "modify", description: "Record revenue", riskLevel: "LOW", reversible: true, auditRequired: true },
    { name: "projects.create_task", permission: "create", description: "Create an internal task", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "projects.update_task", permission: "modify", description: "Update a task", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "projects.complete_task", permission: "modify", description: "Complete a task", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "marketing.get_campaign", permission: "read", description: "Read a campaign", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "marketing.list_campaigns", permission: "read", description: "List campaigns", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "marketing.create_experiment", permission: "execute", description: "Create a marketing experiment", riskLevel: "LOW", reversible: true, auditRequired: true },
    { name: "marketing.spend", permission: "spend", description: "Spend on marketing", riskLevel: "MEDIUM", reversible: false, auditRequired: true },
    { name: "executive.list_approvals", permission: "read", description: "List approvals", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "executive.list_decisions", permission: "read", description: "List decisions", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "executive.list_risks", permission: "read", description: "List risks", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "executive.list_tasks", permission: "read", description: "List tasks", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "executive.list_products", permission: "read", description: "List products", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "executive.list_activity", permission: "read", description: "List audit activity", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "website.get_status", permission: "read", description: "Read website status", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "website.get_page", permission: "read", description: "Fetch a website page", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "executive.create_decision", permission: "create", description: "Create a decision record", riskLevel: "LOW", reversible: true, auditRequired: true },
    { name: "executive.request_approval", permission: "create", description: "Request approval", riskLevel: "LOW", reversible: true, auditRequired: true },
    { name: "executive.create_task", permission: "create", description: "Create a task from a decision", riskLevel: "LOW", reversible: true, auditRequired: true },
    { name: "executive.request_local_task", permission: "create", description: "Queue a local-machine task", riskLevel: "LOW", reversible: true, auditRequired: true },
    { name: "executive.delegate_idea", permission: "create", description: "Delegate an idea to the local machine with a structured brief", riskLevel: "LOW", reversible: true, auditRequired: true },
    { name: "executive.get_local_task", permission: "read", description: "Read a local task status", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "executive.list_local_tasks", permission: "read", description: "List local tasks", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "executive.list_files", permission: "read", description: "List uploaded files", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "executive.get_file", permission: "read", description: "Read an uploaded file", riskLevel: "LOW", reversible: true, auditRequired: false },
    { name: "executive.modify_doctrine", permission: "doctrine_change", description: "Propose a doctrine change", riskLevel: "HIGH", reversible: false, auditRequired: true },
    { name: "executive.delete_data", permission: "delete", description: "Delete production data", riskLevel: "CRITICAL", reversible: false, auditRequired: true },
    { name: "executive.transfer_asset", permission: "ownership_transfer", description: "Transfer ownership/assets", riskLevel: "CRITICAL", reversible: false, auditRequired: true },
  ];
}

export function defaultAuthorityConfig(): AuthorityConfig {
  return {
    spendApprovalThreshold: 100,
    maxAutonomousSpend: 25,
    autonomyMode: "CONTROLLED",
    delegateMode: false,
    fullAccess: false,
  };
}

export function defaultEmergency(): EmergencyState {
  return { paused: false, spendingDisabled: false, deploymentsDisabled: false };
}
