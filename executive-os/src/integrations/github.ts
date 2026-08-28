/**
 * GitHub integration (spec §14).
 *
 * The agent can inspect and modify repositories — including the private
 * Zarlino plugins repo AND the public zarlino-website / zarlino-feedback
 * repos (website source). Every repo-scoped tool accepts optional `owner` /
 * `repo` params so the model can point at any of the org's repos; they
 * default to GITHUB_OWNER/GITHUB_REPO. Production code modification and
 * deployment require Founder approval (enforced by the authority layer).
 */

import { z } from "zod";
import type { Tool } from "../core/tools/types";

const GH_API = "https://api.github.com";

function ghHeaders(ctx: { env: Record<string, string | undefined> }): Record<string, string> {
  const token = ctx.env.GITHUB_TOKEN;
  return {
    Accept: "application/vnd.github+json",
    "X-GitHub-Api-Version": "2022-11-28",
    "User-Agent": "zarlino-executive-os",
    ...(token ? { Authorization: `Bearer ${token}` } : {}),
  };
}

async function ghFetch(ctx: { env: Record<string, string | undefined> }, path: string, init?: RequestInit): Promise<unknown> {
  const res = await fetch(`${GH_API}${path}`, {
    ...init,
    headers: { ...ghHeaders(ctx), ...(init?.headers ?? {}) },
  });
  if (!res.ok) {
    const text = await res.text().catch(() => "");
    const hint = ctx.env.GITHUB_TOKEN
      ? "GitHub API returned an error"
      : "Set GITHUB_TOKEN (wrangler secret put GITHUB_TOKEN) — the zarlino-audio repos are private/rate-limited";
    throw new Error(`GitHub API ${res.status}: ${text.slice(0, 200)} — ${hint}`);
  }
  if (res.status === 204) return { ok: true };
  return res.json();
}

/** Resolve owner/repo from optional input params, falling back to env. */
function resolveRepo(
  ctx: { env: Record<string, string | undefined> },
  input: { owner?: string; repo?: string },
): { owner: string; repo: string } {
  const owner = ctx.env.GITHUB_OWNER ?? "zarlino-audio";
  const repo = ctx.env.GITHUB_REPO ?? "Zarlino";
  return { owner: input.owner ?? owner, repo: input.repo ?? repo };
}

/** Shared optional owner/repo fields for every repo-scoped tool. */
const repoFields = { owner: z.string().min(1).max(128).optional(), repo: z.string().min(1).max(128).optional() };

/**
 * Slice decoded file content into a model-sized chunk.
 *
 * Large source files (40KB+ workers) used to be unreadable past the first
 * ~8KB because every read started at byte 0 and the model-facing truncator
 * cut the same prefix. Paging fixes that: the agent passes `offset` to read
 * the next chunk, and the metadata (`totalChars`, `hasMore`, next offset)
 * lets it page through the whole file.
 */
export function sliceContentChunk(
  content: string,
  offset: number,
  maxBytes?: number,
): { content: string; offset: number; totalChars: number; returnedChars: number; hasMore: boolean } {
  const totalChars = content.length;
  const start = Math.max(0, Math.min(Math.trunc(offset) || 0, totalChars));
  const end = maxBytes && maxBytes > 0 ? start + Math.trunc(maxBytes) : totalChars;
  const chunk = content.slice(start, end);
  return {
    content: chunk,
    offset: start,
    totalChars,
    returnedChars: chunk.length,
    hasMore: end < totalChars,
  };
}

export function githubTools(ctx: { env: Record<string, string | undefined> }): Tool[] {
  return [
    {
      name: "github.read_repo",
      description: "Read repository metadata (owner/repo optional; default = GITHUB_REPO)",
      inputSchema: z.object({ ...repoFields }),
      requiredPermission: "read",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: false,
      run: async (_tctx, input) => {
        const { owner, repo } = resolveRepo(ctx, input);
        return ghFetch(ctx, `/repos/${owner}/${repo}`);
      },
    },
    {
      name: "github.list_issues",
      description: "List repository issues (optionally filtered by state). owner/repo optional — pass repo='zarlino-website' to read website source issues.",
      inputSchema: z.object({ ...repoFields, state: z.enum(["open", "closed", "all"]).optional(), limit: z.number().int().min(1).max(100).optional() }),
      requiredPermission: "read",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: false,
      run: async (_tctx, input) => {
        const { owner, repo } = resolveRepo(ctx, input);
        const state = input.state ?? "open";
        const perPage = input.limit ?? 30;
        return ghFetch(ctx, `/repos/${owner}/${repo}/issues?state=${state}&per_page=${perPage}`);
      },
    },
    {
      name: "github.get_issue",
      description: "Read a single issue by number. owner/repo optional.",
      inputSchema: z.object({ ...repoFields, issueNumber: z.number().int().positive() }),
      requiredPermission: "read",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: false,
      run: async (_tctx, input) => {
        const { owner, repo } = resolveRepo(ctx, input);
        return ghFetch(ctx, `/repos/${owner}/${repo}/issues/${input.issueNumber}`);
      },
    },
    {
      name: "github.search_issues",
      description: "Search issues/PRs with a GitHub search query",
      inputSchema: z.object({ query: z.string().min(1).max(256) }),
      requiredPermission: "analyze",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: false,
      run: async (_tctx, input) => {
        return ghFetch(ctx, `/search/issues?q=${encodeURIComponent(input.query)}`);
      },
    },
    {
      name: "github.create_issue",
      description: "Create an issue (owner/repo optional — default GITHUB_REPO, e.g. pass repo='zarlino-website')",
      inputSchema: z.object({ ...repoFields, title: z.string().min(1).max(256), body: z.string().max(8000).optional(), labels: z.array(z.string()).optional() }),
      requiredPermission: "create",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: true,
      run: async (_tctx, input) => {
        const { owner, repo } = resolveRepo(ctx, input);
        return ghFetch(ctx, `/repos/${owner}/${repo}/issues`, {
          method: "POST",
          body: JSON.stringify({ title: input.title, body: input.body, labels: input.labels }),
        });
      },
    },
    {
      name: "github.update_issue",
      description: "Update issue status (state, title, body, labels). owner/repo optional.",
      inputSchema: z.object({
        ...repoFields,
        issueNumber: z.number().int().positive(),
        state: z.enum(["open", "closed"]).optional(),
        title: z.string().max(256).optional(),
        body: z.string().max(8000).optional(),
      }),
      requiredPermission: "modify",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: true,
      run: async (_tctx, input) => {
        const { owner, repo } = resolveRepo(ctx, input);
        const { issueNumber, ...patch } = input;
        return ghFetch(ctx, `/repos/${owner}/${repo}/issues/${issueNumber}`, {
          method: "PATCH",
          body: JSON.stringify(patch),
        });
      },
    },
    {
      name: "github.list_pulls",
      description: "List open pull requests. owner/repo optional.",
      inputSchema: z.object({ ...repoFields, state: z.enum(["open", "closed", "all"]).optional(), limit: z.number().int().min(1).max(100).optional() }),
      requiredPermission: "read",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: false,
      run: async (_tctx, input) => {
        const { owner, repo } = resolveRepo(ctx, input);
        const state = input.state ?? "open";
        return ghFetch(ctx, `/repos/${owner}/${repo}/pulls?state=${state}&per_page=${input.limit ?? 30}`);
      },
    },
    {
      name: "github.read_commit",
      description: "Read a commit by SHA or ref. owner/repo optional.",
      inputSchema: z.object({ ...repoFields, ref: z.string().min(1).max(128) }),
      requiredPermission: "read",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: false,
      run: async (_tctx, input) => {
        const { owner, repo } = resolveRepo(ctx, input);
        return ghFetch(ctx, `/repos/${owner}/${repo}/commits/${encodeURIComponent(input.ref)}`);
      },
    },
    {
      name: "github.list_workflows",
      description: "List GitHub Actions workflows in the repository. owner/repo optional.",
      inputSchema: z.object({ ...repoFields, perPage: z.number().int().min(1).max(100).optional() }),
      requiredPermission: "read",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: false,
      run: async (_tctx, input) => {
        const { owner, repo } = resolveRepo(ctx, input);
        return ghFetch(ctx, `/repos/${owner}/${repo}/actions/workflows?per_page=${input.perPage ?? 100}`);
      },
    },
    {
      name: "github.trigger_workflow",
      description: "Trigger a GitHub Actions workflow (CI/release) by workflow file name or id. owner/repo optional.",
      inputSchema: z.object({
        ...repoFields,
        workflow: z.string().min(1).max(128),
        ref: z.string().min(1).max(128).optional(),
        inputs: z.record(z.string(), z.union([z.string(), z.number(), z.boolean()])).optional(),
      }),
      requiredPermission: "execute",
      riskLevel: "MEDIUM",
      reversible: true,
      auditRequired: true,
      run: async (_tctx, input) => {
        const { owner, repo } = resolveRepo(ctx, input);
        const workflow = /^\d+$/.test(input.workflow) ? input.workflow : encodeURIComponent(input.workflow);
        await ghFetch(ctx, `/repos/${owner}/${repo}/actions/workflows/${workflow}/dispatches`, {
          method: "POST",
          body: JSON.stringify({
            ref: input.ref ?? "main",
            inputs: input.inputs ?? {},
          }),
        });
        return {
          triggered: true,
          workflow: input.workflow,
          ref: input.ref ?? "main",
          note: "Workflow dispatched via GitHub Actions. Check run status via the Actions tab.",
        };
      },
    },
    {
      name: "github.list_branches",
      description: "List branches in the repository. owner/repo optional.",
      inputSchema: z.object({ ...repoFields, perPage: z.number().int().min(1).max(100).optional() }),
      requiredPermission: "read",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: false,
      run: async (_tctx, input) => {
        const { owner, repo } = resolveRepo(ctx, input);
        return ghFetch(ctx, `/repos/${owner}/${repo}/branches?per_page=${input.perPage ?? 100}`);
      },
    },
    {
      name: "github.create_branch",
      description: "Create a branch from the default branch (or a given base). owner/repo optional.",
      inputSchema: z.object({ ...repoFields, branch: z.string().min(1).max(128), base: z.string().min(1).max(128).optional() }),
      requiredPermission: "execute",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: true,
      run: async (_tctx, input) => {
        const { owner, repo } = resolveRepo(ctx, input);
        const base = input.base ?? "main";
        const ref = await ghFetch(ctx, `/repos/${owner}/${repo}/git/ref/heads/${encodeURIComponent(base)}`) as { object?: { sha?: string } };
        const sha = ref.object?.sha;
        if (!sha) throw new Error(`Could not resolve base ref "${base}"`);
        await ghFetch(ctx, `/repos/${owner}/${repo}/git/refs`, {
          method: "POST",
          body: JSON.stringify({ ref: `refs/heads/${input.branch}`, sha }),
        });
        return { created: true, branch: input.branch, base, sha: sha.slice(0, 7) };
      },
    },
    {
      name: "github.write_file",
      description: "Create or update a file in the repository (Contents API) on a branch. owner/repo optional — pass repo='zarlino-website' to edit website source.",
      inputSchema: z.object({
        ...repoFields,
        path: z.string().min(1).max(256),
        content: z.string().min(1).max(200_000),
        message: z.string().min(1).max(256),
        branch: z.string().min(1).max(128).optional(),
      }),
      requiredPermission: "modify",
      riskLevel: "MEDIUM",
      reversible: true,
      auditRequired: true,
      run: async (_tctx, input) => {
        const { owner, repo } = resolveRepo(ctx, input);
        // Base64 (UTF-8 safe) for the Contents API.
        const content = btoa(unescape(encodeURIComponent(input.content)));
        const encodedPath = input.path.split("/").map(encodeURIComponent).join("/");
        // If the file exists, include its SHA so we update rather than 422.
        let sha: string | undefined;
        try {
          const existing = await ghFetch(ctx, `/repos/${owner}/${repo}/contents/${encodedPath}?ref=${encodeURIComponent(input.branch ?? "main")}`) as { sha?: string };
          sha = existing.sha;
        } catch {
          sha = undefined;
        }
        const res = await ghFetch(ctx, `/repos/${owner}/${repo}/contents/${encodedPath}`, {
          method: "PUT",
          body: JSON.stringify({
            message: input.message,
            content,
            branch: input.branch ?? "main",
            ...(sha ? { sha } : {}),
          }),
        });
        const r = res as { commit?: { sha?: string } };
        return { written: true, path: input.path, owner, repo, branch: input.branch ?? "main", commit: r.commit?.sha?.slice(0, 7) };
      },
    },
    {
      name: "github.create_pull_request",
      description: "Open a pull request. owner/repo optional.",
      inputSchema: z.object({
        ...repoFields,
        title: z.string().min(1).max(256),
        head: z.string().min(1).max(128),
        base: z.string().min(1).max(128).optional(),
        body: z.string().max(8000).optional(),
      }),
      requiredPermission: "execute",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: true,
      run: async (_tctx, input) => {
        const { owner, repo } = resolveRepo(ctx, input);
        return ghFetch(ctx, `/repos/${owner}/${repo}/pulls`, {
          method: "POST",
          body: JSON.stringify({ title: input.title, head: input.head, base: input.base ?? "main", body: input.body }),
        });
      },
    },
    {
      name: "github.merge_pull_request",
      description: "Merge a pull request (produces a merge commit). owner/repo optional.",
      inputSchema: z.object({ ...repoFields, pullNumber: z.number().int().positive(), commitTitle: z.string().max(256).optional() }),
      requiredPermission: "execute",
      riskLevel: "MEDIUM",
      reversible: false,
      auditRequired: true,
      run: async (_tctx, input) => {
        const { owner, repo } = resolveRepo(ctx, input);
        const res = await ghFetch(ctx, `/repos/${owner}/${repo}/pulls/${input.pullNumber}/merge`, {
          method: "PUT",
          body: JSON.stringify({ commit_title: input.commitTitle, merge_method: "squash" }),
        });
        return res;
      },
    },
    {
      name: "github.list_workflow_runs",
      description: "List recent GitHub Actions workflow runs (optionally for one workflow). owner/repo optional.",
      inputSchema: z.object({ ...repoFields, workflow: z.string().max(128).optional(), limit: z.number().int().min(1).max(100).optional() }),
      requiredPermission: "read",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: false,
      run: async (_tctx, input) => {
        const { owner, repo } = resolveRepo(ctx, input);
        const per = input.limit ?? 20;
        const path = input.workflow
          ? `/repos/${owner}/${repo}/actions/workflows/${encodeURIComponent(input.workflow)}/runs?per_page=${per}`
          : `/repos/${owner}/${repo}/actions/runs?per_page=${per}`;
        return ghFetch(ctx, path);
      },
    },
    {
      name: "github.get_workflow_run",
      description: "Get a single GitHub Actions run and its status/conclusion. owner/repo optional.",
      inputSchema: z.object({ ...repoFields, runId: z.number().int().positive() }),
      requiredPermission: "read",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: false,
      run: async (_tctx, input) => {
        const { owner, repo } = resolveRepo(ctx, input);
        return ghFetch(ctx, `/repos/${owner}/${repo}/actions/runs/${input.runId}`);
      },
    },
    {
      name: "github.list_releases",
      description: "List releases of the repository. owner/repo optional.",
      inputSchema: z.object({ ...repoFields, limit: z.number().int().min(1).max(100).optional() }),
      requiredPermission: "read",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: false,
      run: async (_tctx, input) => {
        const { owner, repo } = resolveRepo(ctx, input);
        return ghFetch(ctx, `/repos/${owner}/${repo}/releases?per_page=${input.limit ?? 30}`);
      },
    },
    {
      name: "github.get_file",
      description:
        "Read a file's content from the repository (decoded) at a branch/ref. owner/repo optional — pass repo='zarlino-website' to read website source. LARGE FILES ARE PAGED: pass offset=N to read the next chunk (the response reports totalChars + hasMore and tells you the next offset). Default maxBytes=6000 so the result fits the model context.",
      inputSchema: z.object({
        ...repoFields,
        path: z.string().min(1).max(256),
        ref: z.string().max(128).optional(),
        offset: z.number().int().min(0).optional(),
        maxBytes: z.number().int().min(200).max(30_000).optional(),
      }),
      requiredPermission: "read",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: false,
      run: async (_tctx, input) => {
        const { owner, repo } = resolveRepo(ctx, input);
        const ref = input.ref ? `?ref=${encodeURIComponent(input.ref)}` : "";
        const res = (await ghFetch(ctx, `/repos/${owner}/${repo}/contents/${input.path.split("/").map(encodeURIComponent).join("/")}${ref}`)) as { content?: string; encoding?: string; size?: number };
        if (res.encoding === "base64" && res.content) {
          const decoded = Buffer.from(res.content, "base64").toString("utf8");
          const chunk = sliceContentChunk(decoded, input.offset ?? 0, input.maxBytes ?? 6_000);
          return {
            path: input.path,
            owner,
            repo,
            ref: input.ref ?? "default",
            size: res.size,
            offset: chunk.offset,
            totalChars: chunk.totalChars,
            returnedChars: chunk.returnedChars,
            hasMore: chunk.hasMore,
            note: chunk.hasMore
              ? `File is ${chunk.totalChars} chars; you are seeing chars ${chunk.offset}–${chunk.offset + chunk.returnedChars}. Re-call with offset=${chunk.offset + chunk.returnedChars} to read the next chunk.`
              : undefined,
            content: chunk.content,
          };
        }
        return res;
      },
    },
    {
      name: "github.list_commits",
      description: "List recent commits on a branch. owner/repo optional.",
      inputSchema: z.object({ ...repoFields, branch: z.string().max(128).optional(), limit: z.number().int().min(1).max(100).optional() }),
      requiredPermission: "read",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: false,
      run: async (_tctx, input) => {
        const { owner, repo } = resolveRepo(ctx, input);
        const sha = input.branch ? `?sha=${encodeURIComponent(input.branch)}` : "";
        return ghFetch(ctx, `/repos/${owner}/${repo}/commits${sha ? `${sha}&` : "?"}per_page=${input.limit ?? 30}`);
      },
    },
    {
      name: "github.list_repos",
      description: "List repositories for an organization OR the authenticated user (zarlino-audio). Tries /orgs/{org}/repos first, falls back to /user/repos.",
      inputSchema: z.object({ org: z.string().max(128).optional(), limit: z.number().int().min(1).max(100).optional() }),
      requiredPermission: "read",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: false,
      run: async (_tctx, input) => {
        const per = input.limit ?? 30;
        const org = input.org ?? ctx.env.GITHUB_OWNER ?? "zarlino-audio";
        // Try the org endpoint first; if the account is actually a user (not
        // an org), GitHub 404s — fall back to the authenticated user's repos.
        try {
          return await ghFetch(ctx, `/orgs/${org}/repos?per_page=${per}&sort=updated`);
        } catch (err) {
          if (err instanceof Error && /404/.test(err.message)) {
            return ghFetch(ctx, `/user/repos?per_page=${per}&sort=updated`);
          }
          throw err;
        }
      },
    },
    {
      name: "github.list_collaborators",
      description: "List repository collaborators (people with access). owner/repo optional.",
      inputSchema: z.object({ ...repoFields, perPage: z.number().int().min(1).max(100).optional() }),
      requiredPermission: "read",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: false,
      run: async (_tctx, input) => {
        const { owner, repo } = resolveRepo(ctx, input);
        return ghFetch(ctx, `/repos/${owner}/${repo}/collaborators?per_page=${input.perPage ?? 100}`);
      },
    },
    {
      name: "github.get_commit_diff",
      description: "Get the diff (patch) of a commit, or compare two refs. owner/repo optional.",
      inputSchema: z.object({ ...repoFields, ref: z.string().min(1).max(128) }),
      requiredPermission: "read",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: false,
      run: async (_tctx, input) => {
        const { owner, repo } = resolveRepo(ctx, input);
        const res = await fetch(`${GH_API}/repos/${owner}/${repo}/commits/${encodeURIComponent(input.ref)}`, {
          headers: { ...ghHeaders(ctx), Accept: "application/vnd.github.v3.diff" },
        });
        if (!res.ok) throw new Error(`GitHub API ${res.status}`);
        const text = await res.text();
        return { ref: input.ref, owner, repo, diff: text.slice(0, 50_000) };
      },
    },
    {
      name: "github.get_file_metadata",
      description: "Get a file's metadata (size, sha, encoding, last commit) without the content. owner/repo optional.",
      inputSchema: z.object({ ...repoFields, path: z.string().min(1).max(256), ref: z.string().max(128).optional() }),
      requiredPermission: "read",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: false,
      run: async (_tctx, input) => {
        const { owner, repo } = resolveRepo(ctx, input);
        const ref = input.ref ? `?ref=${encodeURIComponent(input.ref)}` : "";
        return ghFetch(ctx, `/repos/${owner}/${repo}/contents/${input.path.split("/").map(encodeURIComponent).join("/")}${ref}`);
      },
    },
    {
      name: "github.get_website_source",
      description:
        "Read a file from the public zarlino-website repository (the website source). Convenience wrapper around get_file for repo='zarlino-website'. Returns decoded content. LARGE FILES ARE PAGED: pass offset=N to read the next chunk (the response reports totalChars + hasMore and tells you the next offset). Default maxBytes=6000.",
      inputSchema: z.object({
        path: z.string().min(1).max(256),
        ref: z.string().max(128).optional(),
        offset: z.number().int().min(0).optional(),
        maxBytes: z.number().int().min(200).max(30_000).optional(),
      }),
      requiredPermission: "read",
      riskLevel: "LOW",
      reversible: true,
      auditRequired: false,
      run: async (_tctx, input) => {
        const ref = input.ref ? `?ref=${encodeURIComponent(input.ref)}` : "";
        const res = (await ghFetch(ctx, `/repos/zarlino-audio/zarlino-website/contents/${input.path.split("/").map(encodeURIComponent).join("/")}${ref}`)) as { content?: string; encoding?: string; size?: number };
        if (res.encoding === "base64" && res.content) {
          const decoded = Buffer.from(res.content, "base64").toString("utf8");
          const chunk = sliceContentChunk(decoded, input.offset ?? 0, input.maxBytes ?? 6_000);
          return {
            path: input.path,
            repo: "zarlino-website",
            ref: input.ref ?? "default",
            size: res.size,
            offset: chunk.offset,
            totalChars: chunk.totalChars,
            returnedChars: chunk.returnedChars,
            hasMore: chunk.hasMore,
            note: chunk.hasMore
              ? `File is ${chunk.totalChars} chars; you are seeing chars ${chunk.offset}–${chunk.offset + chunk.returnedChars}. Re-call with offset=${chunk.offset + chunk.returnedChars} to read the next chunk.`
              : undefined,
            content: chunk.content,
          };
        }
        return res;
      },
    },
  ];
}
