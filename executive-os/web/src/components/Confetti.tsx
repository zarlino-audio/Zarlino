/**
 * Confetti — a tiny dependency-free celebration burst.
 *
 * Renders ~28 colored squares that fall with random drift/rotation for about
 * a second and a half, then cleans itself up. Used for fun moments: file
 * uploads, ships, approvals, logins. No libraries, pure CSS animation.
 */
import { useEffect, useMemo, useState } from "react";

const COLORS = ["#e8863a", "#3fb950", "#58a6ff", "#d29922", "#f85149", "#bc8cff", "#56d4dd"];

export function Confetti({ trigger }: { trigger: number }) {
  const [alive, setAlive] = useState(false);

  useEffect(() => {
    if (trigger === 0) return;
    setAlive(true);
    const t = setTimeout(() => setAlive(false), 1800);
    return () => clearTimeout(t);
  }, [trigger]);

  const pieces = useMemo(() => {
    return Array.from({ length: 28 }, (_, i) => ({
      left: Math.random() * 100,
      delay: Math.random() * 0.25,
      dur: 0.9 + Math.random() * 0.8,
      color: COLORS[i % COLORS.length],
      size: 6 + Math.random() * 6,
      spin: Math.random() > 0.5 ? 1 : -1,
    }));
  }, []);

  if (!alive) return null;
  return (
    <div className="confetti" aria-hidden="true">
      {pieces.map((p, i) => (
        <span
          key={i}
          className="confetti-piece"
          style={{
            left: `${p.left}%`,
            background: p.color,
            width: p.size,
            height: p.size * 0.5,
            animationDelay: `${p.delay}s`,
            animationDuration: `${p.dur}s`,
            ["--spin" as string]: `${p.spin * 540}deg`,
          }}
        />
      ))}
    </div>
  );
}

export default Confetti;
