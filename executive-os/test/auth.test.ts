import { describe, expect, it } from "vitest";
import {
  generateToken,
  hashPassword,
  isExpired,
  sessionExpiry,
  timingSafeEqual,
  validatePassword,
  validateUsername,
  verifyPassword,
} from "../src/core/auth/passwords";

describe("founder password auth (username + password)", () => {
  it("hashes and verifies passwords (PBKDF2)", async () => {
    const { encoded } = await hashPassword("hunter2secure");
    expect(encoded).toMatch(/^pbkdf2\$\d+\$[^$]+\$[^$]+$/);
    expect(await verifyPassword("hunter2secure", encoded)).toBe(true);
    expect(await verifyPassword("wrongpass", encoded)).toBe(false);
  });

  it("uses a random salt — same password hashes differently", async () => {
    const a = await hashPassword("samepassword");
    const b = await hashPassword("samepassword");
    expect(a.encoded).not.toBe(b.encoded);
  });

  it("generates unique 64-hex session tokens", () => {
    const a = generateToken();
    const b = generateToken();
    expect(a).toMatch(/^[0-9a-f]{64}$/);
    expect(a).not.toBe(b);
  });

  it("validates usernames and passwords", () => {
    expect(validateUsername("founder")).toBeNull();
    expect(validateUsername("ab")).toContain("Username");
    expect(validateUsername("bad name!")).toContain("Username");
    expect(validatePassword("short")).toContain("at least 8");
    expect(validatePassword("goodenough1")).toBeNull();
  });

  it("computes session expiry and expiry checks", () => {
    const exp = sessionExpiry(1_000_000);
    expect(isExpired(exp, 1_000_000)).toBe(false);
    expect(isExpired(exp, 1_000_000 + 30 * 24 * 60 * 60 * 1000 + 1)).toBe(true);
    expect(isExpired("not-a-date")).toBe(true);
  });

  it("compares strings in constant time", () => {
    expect(timingSafeEqual("abcdef", "abcdef")).toBe(true);
    expect(timingSafeEqual("abcdef", "abcdeg")).toBe(false);
    expect(timingSafeEqual("abc", "abcd")).toBe(false);
  });
});
