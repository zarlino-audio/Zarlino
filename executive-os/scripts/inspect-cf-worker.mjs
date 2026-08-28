// Temp diagnostic: inspect the multipart structure of Cloudflare worker GET responses.
const token = process.env.CF_TMP_TOKEN;
const acct = "ae604b602fdbcb1b48b41e975c6dbd19";
if (!token) { console.error("set CF_TMP_TOKEN"); process.exit(1); }
for (const name of ["zarlino-executive-os", "zarlino-website"]) {
  const res = await fetch(`https://api.cloudflare.com/client/v4/accounts/${acct}/workers/scripts/${name}`, {
    headers: { Authorization: `Bearer ${token}` },
  });
  const ct = res.headers.get("content-type") || "";
  console.log("=== " + name + " === content-type: " + ct);
  const body = await res.text();
  if (!ct.includes("multipart")) {
    console.log("  (not multipart) body prefix: " + body.slice(0, 140));
    continue;
  }
  const m = ct.match(/boundary=(?:"([^"]+)"|([^;]+))/);
  const boundary = (m && (m[1] || m[2])) || "";
  const parts = body.split("--" + boundary);
  parts.forEach((p, i) => {
    if (!p.trim()) return;
    const sep = p.search(/\r?\n\r?\n/);
    const header = (sep === -1 ? p : p.slice(0, sep)).replace(/\r?\n/g, " ").slice(0, 160);
    console.log(`  part${i}: ${header}`);
  });
}
