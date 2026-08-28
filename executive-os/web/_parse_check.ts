/** Temporary parser check (deleted after run). */
import { parseQuestionCards } from "./src/views/Chat";

const sample1 =
  "Here is my recommendation.\n\n[QUESTION_CARDS]\n{\"questions\": [{\"label\": \"Approve it\", \"prompt\": \"Approve the affiliate spend decision\"}, {\"label\": \"Tell me more\", \"prompt\": \"Explain the affiliate decision in more detail\"}]}\n[/QUESTION_CARDS]";
const r1 = parseQuestionCards(sample1);
console.log("cards:", JSON.stringify(r1.cards));
console.log("marker stripped:", !r1.text.includes("QUESTION_CARDS"));
console.log("text kept:", r1.text.trim().startsWith("Here is my recommendation"));

const r2 = parseQuestionCards("No cards here");
console.log("no cards:", r2.cards.length === 0 && r2.text === "No cards here");

const r3 = parseQuestionCards("Broken [QUESTION_CARDS]{bad json}[/QUESTION_CARDS] tail");
console.log("bad json stripped:", !r3.text.includes("QUESTION_CARDS") && r3.cards.length === 0 && r3.text === "Broken  tail");
