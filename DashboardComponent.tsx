/**
 * Quantum-Execution-Engine — Live Execution Dashboard
 * Next.js 14 + TypeScript + Tailwind CSS
 *
 * Renders a high-density, real-time latency & throughput table.
 * Designed for institutional monitoring cockpits.
 */

"use client";

import React, { useEffect, useState } from "react";

interface LatencySample {
  id: string;
  stage: string;
  latencyUs: number;
  status: "ok" | "warn" | "critical";
  timestamp: number;
}

const STAGE_ORDER = [
  "UI → FastAPI",
  "FastAPI → Rust",
  "Rust Router",
  "C++ Risk Kernel",
  "Java FIX Adapter",
] as const;

function statusColor(status: LatencySample["status"]): string {
  switch (status) {
    case "ok":
      return "text-emerald-400";
    case "warn":
      return "text-amber-400";
    case "critical":
      return "text-rose-400";
    default:
      return "text-slate-400";
  }
}

export default function DashboardComponent() {
  const [samples, setSamples] = useState<LatencySample[]>([]);

  // Simulated live feed (replace with real WebSocket in production)
  useEffect(() => {
    const interval = setInterval(() => {
      const now = Date.now();
      const next: LatencySample[] = STAGE_ORDER.map((stage, idx) => {
        const base = [120, 45, 28, 8, 65][idx];
        const jitter = Math.random() * 15;
        const latencyUs = Math.round(base + jitter);
        let status: LatencySample["status"] = "ok";
        if (latencyUs > 100) status = "critical";
        else if (latencyUs > 60) status = "warn";

        return {
          id: `\( {stage}- \){now}`,
          stage,
          latencyUs,
          status,
          timestamp: now,
        };
      });
      setSamples(next);
    }, 800);

    return () => clearInterval(interval);
  }, []);

  return (
    <div className="min-h-screen bg-slate-950 text-slate-100 p-6 font-mono">
      <header className="mb-8 border-b border-slate-800 pb-4">
        <h1 className="text-2xl font-bold tracking-tight text-white">
          Quantum Execution Engine
        </h1>
        <p className="text-sm text-slate-400 mt-1">
          Live multi-language latency cockpit · Target budget &lt; 50 µs (router)
        </p>
      </header>

      <div className="overflow-x-auto rounded-lg border border-slate-800">
        <table className="w-full text-sm">
          <thead className="bg-slate-900 text-slate-400 uppercase tracking-wider">
            <tr>
              <th className="px-4 py-3 text-left">Stage</th>
              <th className="px-4 py-3 text-right">Latency (µs)</th>
              <th className="px-4 py-3 text-center">Status</th>
              <th className="px-4 py-3 text-right">Timestamp</th>
            </tr>
          </thead>
          <tbody className="divide-y divide-slate-800">
            {samples.map((s) => (
              <tr key={s.id} className="hover:bg-slate-900/60 transition-colors">
                <td className="px-4 py-3 font-medium text-slate-200">
                  {s.stage}
                </td>
                <td className="px-4 py-3 text-right tabular-nums">
                  {s.latencyUs.toFixed(0)}
                </td>
                <td className={`px-4 py-3 text-center font-semibold ${statusColor(s.status)}`}>
                  {s.status.toUpperCase()}
                </td>
                <td className="px-4 py-3 text-right text-slate-500 tabular-nums">
                  {new Date(s.timestamp).toLocaleTimeString()}
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>

      <footer className="mt-6 text-xs text-slate-500">
        Polyglot path: TypeScript → Python → Rust → C++20 → Java 17
      </footer>
    </div>
  );
}
