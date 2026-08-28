/**
 * Minimal, safe Markdown renderer for chat replies.
 *
 * The model replies with markdown (**bold**, ***bold italic***, `code`, lists,
 * headers, etc.). Rendering it as plain text leaks the raw `***` markers, so
 * we render a small safe subset.
 *
 * The model ALSO occasionally emits literal HTML entities (`&quot;`, `&#39;`,
 * `&amp;`) instead of real characters. Those must be DECODED — but never
 * re-encoded: React escapes every string child when it renders, so `<` / `>`
 * from the model can never become DOM nodes, and any entity text we emit
 * (e.g. `&quot;`) would be displayed literally by React as `&quot;`. So the
 * correct pipeline is decode-only; React handles safety.
 */

import type { ReactNode } from "react";

/** Common named HTML entities → characters. */
const NAMED_ENTITIES: Record<string, string> = {
  quot: '"', amp: "&", apos: "'", lt: "<", gt: ">", nbsp: " ",
  copy: "©", reg: "®", trade: "™", hellip: "…", mdash: "—", ndash: "–",
  lsquo: "‘", rsquo: "’", ldquo: "“", rdquo: "”", bull: "•", middot: "·",
  ensp: " ", emsp: " ", thinsp: " ", minus: "−", times: "×", divide: "÷",
};

/**
 * Decode HTML entities (&quot;, &#39;, &amp;, &lt;, &#123;, &#x1F600;, …)
 * back to their characters. Leaves anything unrecognized untouched so we never
 * mangle text. React escapes string children when rendering, so the decoded
 * `<` / `>` can never become DOM nodes.
 */
export function decodeEntities(s: string): string {
  if (!/[&]/.test(s)) return s;
  return s.replace(/&(#x?[0-9a-fA-F]+|[a-zA-Z]+);/g, (full, entity: string) => {
    if (entity[0] === "#") {
      const isHex = entity[1] === "x" || entity[1] === "X";
      const code = isHex ? parseInt(entity.slice(2), 16) : parseInt(entity.slice(1), 10);
      if (Number.isFinite(code) && code > 0 && code <= 0x10ffff) {
        try {
          return String.fromCodePoint(code);
        } catch {
          return full;
        }
      }
      return full;
    }
    return NAMED_ENTITIES[entity.toLowerCase()] ?? full;
  });
}

/** Decode stray entities for display; React escapes the rest for DOM safety. */
function safe(s: string): string {
  return decodeEntities(s);
}

/** Render inline markdown (bold/italic/code) — input must already be escaped. */
function inline(text: string): ReactNode[] {
  const out: ReactNode[] = [];
  const regex = /(\*\*\*([^*]+)\*\*\*|\*\*([^*]+)\*\*|\*([^*]+)\*|`([^`]+)`)/g;
  let last = 0;
  let m: RegExpExecArray | null;
  let key = 0;
  while ((m = regex.exec(text)) !== null) {
    if (m.index > last) out.push(text.slice(last, m.index));
    if (m[2] !== undefined) {
      out.push(<strong key={key++}><em>{m[2]}</em></strong>);
    } else if (m[3] !== undefined) {
      out.push(<strong key={key++}>{m[3]}</strong>);
    } else if (m[4] !== undefined) {
      out.push(<em key={key++}>{m[4]}</em>);
    } else if (m[5] !== undefined) {
      out.push(<code key={key++}>{m[5]}</code>);
    }
    last = m.index + m[0].length;
  }
  if (last < text.length) out.push(text.slice(last));
  return out;
}

export function Markdown({ text }: { text: string }) {
  const lines = text.split(/\r?\n/);
  const blocks: ReactNode[] = [];
  let list: string[] = [];
  let key = 0;

  const flushList = () => {
    if (list.length === 0) return;
    blocks.push(
      <ul key={key++}>
        {list.map((item, i) => <li key={i}>{inline(safe(item))}</li>)}
      </ul>,
    );
    list = [];
  };

  for (const raw of lines) {
    const line = raw.trimEnd();
    const heading = line.match(/^(#{1,4})\s+(.*)$/);
    const listItem = line.match(/^\s*[-*]\s+(.*)$/);
    const fence = line.trim().startsWith("```");
    if (fence) {
      flushList();
      blocks.push(<pre key={key++}><code>{safe(line.trim().replace(/^```+/, ""))}</code></pre>);
      continue;
    }
    if (listItem) {
      list.push(listItem[1]);
      continue;
    }
    flushList();
    if (heading) {
      const level = Math.min(heading[1].length, 4);
      const text = heading[2];
      if (level === 1) blocks.push(<h4 key={key++}>{inline(safe(text))}</h4>);
      else if (level === 2) blocks.push(<h5 key={key++}>{inline(safe(text))}</h5>);
      else blocks.push(<strong key={key++}>{inline(safe(text))}</strong>);
      continue;
    }
    if (line.trim() === "") {
      continue; // paragraph break handled by spacing below
    }
    blocks.push(<p key={key++}>{inline(safe(line))}</p>);
  }
  flushList();

  return <div className="markdown">{blocks}</div>;
}
