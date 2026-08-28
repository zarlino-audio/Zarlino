import { loadDotEnv } from "./dotenv.js";

const dotenv = loadDotEnv("C:/Users/khrit/Desktop/plugin/executive-os/.dev.vars");
const key = process.env.DEEPSEEK_API_KEY ?? dotenv.DEEPSEEK_API_KEY ?? "";

async function test() {
  const img = await fetch("https://www.google.com/images/branding/googlelogo/2x/googlelogo_color_272x92dp.png");
  const buf = Buffer.from(await img.arrayBuffer());
  const b64 = buf.toString("base64");
  console.log("image bytes:", buf.length);
  const res = await fetch("https://api.deepseek.com/chat/completions", {
    method: "POST",
    headers: { "Content-Type": "application/json", Authorization: `Bearer ${key}` },
    body: JSON.stringify({
      model: "deepseek-v4-flash-vision-exp",
      messages: [
        {
          role: "user",
          content: [
            { type: "text", text: "What is in this image? Describe it in 2-3 sentences." },
            { type: "image_url", image_url: { url: `data:image/png;base64,${b64}` } },
          ],
        },
      ],
      max_tokens: 300,
    }),
  });
  const text = await res.text();
  console.log(`HTTP ${res.status}`);
  console.log(text.slice(0, 1500));
}

test().catch((e) => console.error("ERR", e.message));
