/**
 * DeepSeek API integration — provider account balance.
 *
 * DeepSeek exposes `GET /user/balance` (Bearer key) returning the account's
 * remaining balance. We surface this in the console so the Founder can see
 * exactly how much credit the AI provider still has — separate from the OS's
 * own USD cost budgets (which are enforced against *spend*, not account
 * credit). A low provider balance is the real "can we keep working" signal.
 */

export interface ProviderBalance {
  provider: "deepseek";
  /** Whether the balance endpoint responded OK and the account is usable. */
  isAvailable: boolean;
  /** When the data was fetched (ISO). */
  fetchedAt: string;
  /** Balance line(s) from the provider (currency, total, granted, topped up). */
  balances: Array<{
    currency: string;
    total: string;
    granted: string;
    toppedUp: string;
  }>;
  /** Rough USD equivalent of the total balance (rate-based estimate). */
  totalUsdEstimate: number | null;
  error?: string;
}

/** Rough CNY→USD conversion (rate is a configurable estimate, not a live FX quote). */
export const DEFAULT_CNY_TO_USD = 0.14;

/** Fetch the DeepSeek account balance. Throws on network/auth failure. */
export async function fetchDeepSeekBalance(
  apiKey: string,
  opts: { timeoutMs?: number; cnyToUsd?: number } = {},
): Promise<ProviderBalance> {
  const timeoutMs = opts.timeoutMs ?? 15_000;
  const cnyToUsd = opts.cnyToUsd ?? DEFAULT_CNY_TO_USD;
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs);
  try {
    const res = await fetch("https://api.deepseek.com/user/balance", {
      headers: { Authorization: `Bearer ${apiKey}` },
      signal: controller.signal,
    });
    if (!res.ok) {
      const body = await res.text().catch(() => "");
      throw new Error(`DeepSeek balance HTTP ${res.status}: ${body.slice(0, 200)}`);
    }
    const data = (await res.json()) as {
      is_available?: boolean;
      balance_infos?: Array<{ currency?: string; total_balance?: string; granted_balance?: string; topped_up_balance?: string }>;
    };
    const balances = (data.balance_infos ?? []).map((b) => ({
      currency: b.currency ?? "CNY",
      total: b.total_balance ?? "0",
      granted: b.granted_balance ?? "0",
      toppedUp: b.topped_up_balance ?? "0",
    }));
    // Estimate USD for the primary (first) balance line when it's CNY.
    let totalUsdEstimate: number | null = null;
    const primary = balances[0];
    if (primary && primary.currency.toUpperCase() === "CNY") {
      const total = Number(primary.total);
      if (Number.isFinite(total)) totalUsdEstimate = Math.round(total * cnyToUsd * 100) / 100;
    }
    return {
      provider: "deepseek",
      isAvailable: data.is_available !== false,
      fetchedAt: new Date().toISOString(),
      balances,
      totalUsdEstimate,
    };
  } finally {
    clearTimeout(timer);
  }
}
