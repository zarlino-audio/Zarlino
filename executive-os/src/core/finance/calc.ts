/**
 * Financial calculations (spec §8).
 *
 * Financial recommendations must be numerical whenever possible and must
 * distinguish Known / Estimated / Assumed / Unknown. These functions are
 * pure: they return `null` (UNKNOWN) whenever required inputs are missing —
 * they never fabricate financial data.
 */

export interface CashFlow {
  cash?: number;
  monthlyRevenue?: number;
  monthlyExpenses?: number;
}

/** Monthly net burn. Negative = profitable. UNKNOWN when inputs missing. */
export function netBurn(input: CashFlow): number | null {
  if (input.monthlyRevenue === undefined || input.monthlyExpenses === undefined) return null;
  return input.monthlyExpenses - input.monthlyRevenue;
}

/** Runway in months. UNKNOWN when cash or burn is missing. */
export function runwayMonths(input: CashFlow): number | null {
  if (input.cash === undefined) return null;
  const burn = netBurn(input);
  if (burn === null) return null;
  if (burn <= 0) return Number.POSITIVE_INFINITY; // not burning cash
  return input.cash / burn;
}

/** Simple ROI as a ratio (gain - cost) / cost. UNKNOWN when cost is 0/undefined. */
export function roi(cost: number | undefined, gain: number | undefined): number | null {
  if (cost === undefined || gain === undefined || cost <= 0) return null;
  return (gain - cost) / cost;
}

/** Expected value of a bet: p*gain - (1-p)*loss. */
export function expectedValue(probability: number, gain: number, loss: number): number {
  return probability * gain - (1 - probability) * loss;
}

/** Customer acquisition cost. UNKNOWN when no customers acquired. */
export function cac(marketingSpend: number | undefined, newCustomers: number | undefined): number | null {
  if (marketingSpend === undefined || newCustomers === undefined || newCustomers <= 0) return null;
  return marketingSpend / newCustomers;
}

/** Payback months: CAC / (monthlyRevenuePerCustomer * grossMargin). */
export function paybackMonths(
  customerAcquisitionCost: number | undefined,
  monthlyRevenuePerCustomer: number | undefined,
  grossMargin: number | undefined,
): number | null {
  if (
    customerAcquisitionCost === undefined ||
    monthlyRevenuePerCustomer === undefined ||
    grossMargin === undefined ||
    grossMargin <= 0
  ) {
    return null;
  }
  const monthlyContribution = monthlyRevenuePerCustomer * grossMargin;
  if (monthlyContribution <= 0) return null;
  return customerAcquisitionCost / monthlyContribution;
}

/** Gross margin ratio. UNKNOWN when revenue is 0/undefined. */
export function grossMargin(revenue: number | undefined, costOfGoods: number | undefined): number | null {
  if (revenue === undefined || costOfGoods === undefined || revenue <= 0) return null;
  return (revenue - costOfGoods) / revenue;
}

export interface FinancialAssessment {
  runwayMonths: number | null;
  netBurn: number | null;
  grossMargin: number | null;
  roi: number | null;
  expectedValue: number | null;
  cac: number | null;
  paybackMonths: number | null;
  /** Human-readable flags about what is UNKNOWN. */
  unknown: string[];
}

/**
 * Produce a structured financial assessment for a proposed spend/decision.
 * Missing data is reported as UNKNOWN — never invented.
 */
export function assessFinancials(input: {
  cash?: number;
  monthlyRevenue?: number;
  monthlyExpenses?: number;
  revenue?: number;
  costOfGoods?: number;
  proposedCost?: number;
  expectedGain?: number;
  probabilityOfSuccess?: number;
  marketingSpend?: number;
  newCustomers?: number;
  monthlyRevenuePerCustomer?: number;
  grossMarginRate?: number;
}): FinancialAssessment {
  const unknown: string[] = [];
  const rm = runwayMonths(input);
  if (rm === null) unknown.push("runway");
  const nb = netBurn(input);
  if (nb === null) unknown.push("net burn");
  const gm = grossMargin(input.revenue, input.costOfGoods);
  if (gm === null && input.revenue !== undefined) unknown.push("gross margin");
  const r = roi(input.proposedCost, input.expectedGain);
  if (r === null && input.proposedCost !== undefined) unknown.push("ROI");
  const ev =
    input.probabilityOfSuccess !== undefined && input.expectedGain !== undefined && input.proposedCost !== undefined
      ? expectedValue(input.probabilityOfSuccess, input.expectedGain, input.proposedCost)
      : null;
  if (ev === null && input.probabilityOfSuccess !== undefined) unknown.push("expected value");
  const c = cac(input.marketingSpend, input.newCustomers);
  if (c === null && input.marketingSpend !== undefined) unknown.push("CAC");
  const payback = paybackMonths(c ?? undefined, input.monthlyRevenuePerCustomer, input.grossMarginRate);
  if (payback === null && c !== null) unknown.push("payback");

  return { runwayMonths: rm, netBurn: nb, grossMargin: gm, roi: r, expectedValue: ev, cac: c, paybackMonths: payback, unknown };
}
