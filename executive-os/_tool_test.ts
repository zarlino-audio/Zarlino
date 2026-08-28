/** Temp tool harness (deleted after run). */
import { TOOLS } from "./scripts/local-agent";

const name = process.argv[2] ?? "local_render_ab";
const args = process.env.TOOL_ARGS ? JSON.parse(process.env.TOOL_ARGS) : {};
const tool = TOOLS.find((t) => t.name === name);
if (!tool) {
  console.error("Unknown tool:", name);
  process.exit(1);
}
const out = await tool.run(args);
console.log(out);
