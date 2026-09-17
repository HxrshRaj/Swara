import { useMemo, useRef, useState } from "react";
import "./App.css";

const API_URL = import.meta.env.VITE_API_URL || "http://127.0.0.1:8000";

const EFFECTS = [
  { key: "lowpass", label: "Lowpass filter (Butterworth IIR)" },
  { key: "highpass", label: "Highpass filter (Butterworth IIR)" },
  { key: "delay", label: "Delay / echo (circular buffer)" },
  { key: "noisegate", label: "Spectral noise gate (STFT / FFT)" },
];

const BUFFER_SIZES = [64, 128, 256, 512, 1024, 2048, 4096];

function LatencyTable({ stats }) {
  if (!stats) return null;
  const entries = Object.entries(stats.main_run_latency_us || {});
  const bufferSize = stats.buffer_size;
  const sampleRate = stats.sample_rate;
  const budgetUs = sampleRate ? (bufferSize / sampleRate) * 1_000_000 : null;

  return (
    <div className="latency-block">
      <h3>Measured latency (this run)</h3>
      <p className="latency-note">
        Buffer size {bufferSize} samples @ {sampleRate} Hz
        {budgetUs ? ` — real-time budget ${budgetUs.toFixed(0)} µs per buffer` : ""}
      </p>
      <table>
        <thead>
          <tr>
            <th>Stage</th>
            <th>mean</th>
            <th>p50</th>
            <th>p95</th>
            <th>p99</th>
            <th>max</th>
          </tr>
        </thead>
        <tbody>
          {entries.map(([name, s]) => (
            <tr key={name}>
              <td>{name}</td>
              <td>{s.mean_us.toFixed(1)} µs</td>
              <td>{s.p50_us.toFixed(1)} µs</td>
              <td>{s.p95_us.toFixed(1)} µs</td>
              <td>{s.p99_us.toFixed(1)} µs</td>
              <td>{s.max_us.toFixed(1)} µs</td>
            </tr>
          ))}
        </tbody>
      </table>
      <p className="latency-hint">
        These numbers are wall-clock time measured around each buffer-in →
        buffer-out call inside the C++ engine, not an estimate.
      </p>
    </div>
  );
}

export default function App() {
  const [file, setFile] = useState(null);
  const [originalUrl, setOriginalUrl] = useState(null);
  const [selected, setSelected] = useState({ lowpass: true, delay: true, noisegate: false, highpass: false });
  const [bufferSize, setBufferSize] = useState(512);
  const [lpCutoff, setLpCutoff] = useState(4000);
  const [hpCutoff, setHpCutoff] = useState(300);
  const [delayMs, setDelayMs] = useState(300);
  const [delayFeedback, setDelayFeedback] = useState(0.35);
  const [delayMix, setDelayMix] = useState(0.35);

  const [loading, setLoading] = useState(false);
  const [error, setError] = useState(null);
  const [processedUrl, setProcessedUrl] = useState(null);
  const [stats, setStats] = useState(null);

  const fileInputRef = useRef(null);

  const effectsCsv = useMemo(
    () => EFFECTS.filter((e) => selected[e.key]).map((e) => e.key).join(","),
    [selected]
  );

  function onFileChange(e) {
    const f = e.target.files?.[0];
    if (!f) return;
    setFile(f);
    setOriginalUrl(URL.createObjectURL(f));
    setProcessedUrl(null);
    setStats(null);
    setError(null);
  }

  function toggleEffect(key) {
    setSelected((s) => ({ ...s, [key]: !s[key] }));
  }

  async function handleSubmit() {
    if (!file) {
      setError("Choose a .wav file first.");
      return;
    }
    if (!effectsCsv) {
      setError("Select at least one effect.");
      return;
    }
    setLoading(true);
    setError(null);
    setProcessedUrl(null);
    setStats(null);

    const form = new FormData();
    form.append("file", file);
    form.append("effects", effectsCsv);
    form.append("buffer_size", String(bufferSize));
    form.append("lp_cutoff", String(lpCutoff));
    form.append("hp_cutoff", String(hpCutoff));
    form.append("delay_ms", String(delayMs));
    form.append("delay_feedback", String(delayFeedback));
    form.append("delay_mix", String(delayMix));

    try {
      const res = await fetch(`${API_URL}/api/process`, { method: "POST", body: form });
      if (!res.ok) {
        const body = await res.json().catch(() => ({}));
        throw new Error(body.detail || `Request failed (${res.status})`);
      }
      const data = await res.json();
      const bytes = Uint8Array.from(atob(data.audio_base64), (c) => c.charCodeAt(0));
      const blob = new Blob([bytes], { type: "audio/wav" });
      setProcessedUrl(URL.createObjectURL(blob));
      setStats(data.stats);
    } catch (e) {
      setError(e.message || String(e));
    } finally {
      setLoading(false);
    }
  }

  return (
    <div className="app">
      <header>
        <h1>Swara</h1>
        <p className="subtitle">
          Real-time audio DSP, actually running: a C++ engine (Butterworth
          IIR filters, an STFT-based spectral noise gate via KissFFT, and a
          circular-buffer delay line) processing your upload in fixed-size
          blocks, wired to a FastAPI backend and this page.
        </p>
      </header>

      <section className="panel">
        <label className="file-drop">
          <input ref={fileInputRef} type="file" accept="audio/wav,.wav" onChange={onFileChange} />
          {file ? file.name : "Choose a .wav file"}
        </label>

        <div className="effects-grid">
          {EFFECTS.map((e) => (
            <label key={e.key} className="effect-toggle">
              <input
                type="checkbox"
                checked={!!selected[e.key]}
                onChange={() => toggleEffect(e.key)}
              />
              {e.label}
            </label>
          ))}
        </div>

        <div className="params-grid">
          <label>
            Buffer size
            <select value={bufferSize} onChange={(e) => setBufferSize(Number(e.target.value))}>
              {BUFFER_SIZES.map((b) => (
                <option key={b} value={b}>{b} samples</option>
              ))}
            </select>
          </label>

          {selected.lowpass && (
            <label>
              Lowpass cutoff (Hz)
              <input type="number" value={lpCutoff} min={20} max={20000}
                     onChange={(e) => setLpCutoff(Number(e.target.value))} />
            </label>
          )}
          {selected.highpass && (
            <label>
              Highpass cutoff (Hz)
              <input type="number" value={hpCutoff} min={20} max={20000}
                     onChange={(e) => setHpCutoff(Number(e.target.value))} />
            </label>
          )}
          {selected.delay && (
            <>
              <label>
                Delay time (ms)
                <input type="number" value={delayMs} min={1} max={2000}
                       onChange={(e) => setDelayMs(Number(e.target.value))} />
              </label>
              <label>
                Feedback
                <input type="number" step="0.05" value={delayFeedback} min={0} max={0.95}
                       onChange={(e) => setDelayFeedback(Number(e.target.value))} />
              </label>
              <label>
                Wet/dry mix
                <input type="number" step="0.05" value={delayMix} min={0} max={1}
                       onChange={(e) => setDelayMix(Number(e.target.value))} />
              </label>
            </>
          )}
        </div>

        <button onClick={handleSubmit} disabled={loading || !file}>
          {loading ? "Processing…" : "Process audio"}
        </button>

        {error && <p className="error">{error}</p>}
      </section>

      {(originalUrl || processedUrl) && (
        <section className="panel">
          <div className="audio-compare">
            <div>
              <h3>Original</h3>
              {originalUrl && <audio controls src={originalUrl} />}
            </div>
            <div>
              <h3>Processed</h3>
              {processedUrl ? (
                <audio controls src={processedUrl} />
              ) : (
                <p className="muted">Run processing to hear the result</p>
              )}
            </div>
          </div>
        </section>
      )}

      {stats && (
        <section className="panel">
          <LatencyTable stats={stats} />
        </section>
      )}

      <footer>
        <p>
          Backend: <code>{API_URL}</code> —{" "}
          <a href={`${API_URL}/api/health`} target="_blank" rel="noreferrer">health check</a>
        </p>
      </footer>
    </div>
  );
}
