/** Temporary decode check (deleted after run). */
import { decodeEntities } from "./src/components/Markdown";

const cases: Array<[string, string]> = [
  [`The core objective (&quot;move ZScorch and ZTame to release&quot;) is ~done`, `The core objective ("move ZScorch and ZTame to release") is ~done`],
  [`it&#39;s the one thing`, `it's the one thing`],
  [`Tom &amp; Jerry`, `Tom & Jerry`],
  [`&lt;script&gt;alert(1)&lt;/script&gt;`, `<script>alert(1)</script>`],
  [`A &#x1F600; smile`, `A 😀 smile`],
  [`plain text`, `plain text`],
  [`&unknown; stays`, `&unknown; stays`],
];
let ok = true;
for (const [input, expected] of cases) {
  const got = decodeEntities(input);
  const pass = got === expected;
  if (!pass) ok = false;
  console.log(`${pass ? "PASS" : "FAIL"}: ${JSON.stringify(input)} → ${JSON.stringify(got)}${pass ? "" : ` (expected ${JSON.stringify(expected)})`}`);
}
// After re-escaping, quotes/apostrophes must render as the real char in the DOM.
const reescaped = JSON.stringify(input => input).toString();
console.log("all pass:", ok);
