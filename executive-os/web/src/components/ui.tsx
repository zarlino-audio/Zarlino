import type { ReactNode } from "react";

export function Card({ title, children, className }: { title?: string; children: ReactNode; className?: string }) {
  return (
    <div className={`card ${className ?? ""}`}>
      {title && <h3>{title}</h3>}
      {children}
    </div>
  );
}

export function Stat({ label, value, unknown }: { label: string; value: ReactNode; unknown?: boolean }) {
  return (
    <div className="stat">
      <span className="label">{label}</span>
      <span className={`value ${unknown ? "unknown" : ""}`}>{value}</span>
    </div>
  );
}

export function Spinner() {
  return <span className="faint small">loading…</span>;
}

export function ErrorBox({ message }: { message: string }) {
  return <div className="error">Error: {message}</div>;
}

export function fmtMoney(v: number | null | undefined): string {
  if (v === null || v === undefined) return "UNKNOWN";
  return `$${v.toLocaleString(undefined, { minimumFractionDigits: 0, maximumFractionDigits: 2 })}`;
}

export function fmtDate(iso: string): string {
  try {
    return new Date(iso).toISOString().slice(0, 16).replace("T", " ");
  } catch {
    return iso;
  }
}

export function levelClass(level: string): string {
  switch (level) {
    case "LOW": return "green";
    case "MEDIUM": return "yellow";
    case "HIGH": return "yellow";
    case "CRITICAL": return "red";
    default: return "";
  }
}
