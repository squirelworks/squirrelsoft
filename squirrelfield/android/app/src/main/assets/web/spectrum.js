(() => {
  const canvas = document.getElementById("viz");
  const ctx = canvas.getContext("2d");
  const status = document.getElementById("status");
  const btnStart = document.getElementById("btnStart");
  const btnStop = document.getElementById("btnStop");
  const btnHold = document.getElementById("btnHold");
  const btnReset = document.getElementById("btnReset");
  const fftSel = document.getElementById("fftSize");
  const demoTone = document.getElementById("demoTone");

  let audioCtx = null;
  let analyser = null;
  let source = null;
  let stream = null;
  let osc = null;
  let raf = 0;
  let maxHold = false;
  let hold = null;
  let fMin = 0;
  let fMax = 0; // 0 => nyquist
  let freqData = null;
  const waterfall = [];
  const WF_ROWS = 180;

  function resize() {
    const cssW = canvas.clientWidth || 900;
    const ratio = window.devicePixelRatio || 1;
    canvas.width = Math.floor(cssW * ratio);
    canvas.height = Math.floor(640 * ratio);
    ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
  }
  window.addEventListener("resize", resize);
  resize();

  function setStatus(msg, ok) {
    status.innerHTML = ok ? `<strong>${msg}</strong>` : msg;
  }

  async function start() {
    stop();
    audioCtx = new (window.AudioContext || window.webkitAudioContext)();
    const fftSize = parseInt(fftSel.value, 10);
    analyser = audioCtx.createAnalyser();
    analyser.fftSize = fftSize;
    analyser.smoothingTimeConstant = 0.55;
    freqData = new Uint8Array(analyser.frequencyBinCount);
    hold = new Float32Array(analyser.frequencyBinCount);
    hold.fill(-999);
    fMin = 0;
    fMax = audioCtx.sampleRate / 2;

    if (demoTone.checked) {
      osc = audioCtx.createOscillator();
      const gain = audioCtx.createGain();
      gain.gain.value = 0.08;
      osc.frequency.value = 440;
      osc.connect(gain);
      gain.connect(analyser);
      // also hear quietly optional — skip destination to avoid feedback loops with mic
      osc.start();
      setStatus(`Demo 440 Hz · fs=${audioCtx.sampleRate} · fft=${fftSize}`, true);
    } else {
      stream = await navigator.mediaDevices.getUserMedia({
        audio: {
          echoCancellation: false,
          noiseSuppression: false,
          autoGainControl: false
        },
        video: false
      });
      source = audioCtx.createMediaStreamSource(stream);
      source.connect(analyser);
      setStatus(`Mic live · fs=${audioCtx.sampleRate} · fft=${fftSize}`, true);
    }

    btnStart.disabled = true;
    btnStop.disabled = false;
    loop();
  }

  function stop() {
    cancelAnimationFrame(raf);
    if (osc) { try { osc.stop(); } catch (_) {} osc.disconnect(); osc = null; }
    if (source) { source.disconnect(); source = null; }
    if (stream) { stream.getTracks().forEach((t) => t.stop()); stream = null; }
    if (audioCtx) { audioCtx.close(); audioCtx = null; }
    analyser = null;
    btnStart.disabled = false;
    btnStop.disabled = true;
    setStatus("Stopped.");
  }

  function dbColor(v) {
    const t = Math.min(1, Math.max(0, v / 255));
    const r = (20 + 235 * t) | 0;
    const g = (30 + 200 * t * t) | 0;
    const b = (90 + 140 * (1 - t)) | 0;
    return `rgb(${r},${g},${b})`;
  }

  function binRange() {
    const ny = audioCtx.sampleRate / 2;
    const lo = Math.max(0, fMin);
    const hi = Math.min(ny, fMax || ny);
    const n = freqData.length;
    const b0 = Math.floor((lo / ny) * n);
    const b1 = Math.max(b0 + 1, Math.ceil((hi / ny) * n));
    return { b0, b1, lo, hi, ny };
  }

  function loop() {
    raf = requestAnimationFrame(loop);
    if (!analyser) return;
    analyser.getByteFrequencyData(freqData);
    const { b0, b1, lo, hi } = binRange();
    const w = canvas.clientWidth || 900;
    const h = 640;
    const spectrumH = Math.floor(h / 3);

    for (let i = 0; i < freqData.length; i++) {
      if (freqData[i] > hold[i]) hold[i] = freqData[i];
    }

    // waterfall row (visible bins resampled to width)
    const row = new Uint8Array(w);
    for (let x = 0; x < w; x++) {
      const k = b0 + Math.floor((x / w) * (b1 - b0));
      row[x] = freqData[Math.min(freqData.length - 1, k)];
    }
    waterfall.unshift(row);
    if (waterfall.length > WF_ROWS) waterfall.length = WF_ROWS;

    ctx.fillStyle = "#070b16";
    ctx.fillRect(0, 0, w, h);

    // spectrum
    ctx.strokeStyle = "#50dcff";
    ctx.lineWidth = 2;
    ctx.beginPath();
    let peakX = 0, peakV = -1, peakHz = 0;
    for (let x = 0; x < w; x++) {
      const k = b0 + Math.floor((x / w) * (b1 - b0));
      const v = freqData[Math.min(freqData.length - 1, k)];
      const y = spectrumH - 2 - (v / 255) * (spectrumH - 4);
      if (x === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
      if (v > peakV) {
        peakV = v;
        peakX = x;
        peakHz = lo + (x / w) * (hi - lo);
      }
    }
    ctx.stroke();

    if (maxHold) {
      ctx.strokeStyle = "#ffb43c";
      ctx.lineWidth = 1;
      ctx.beginPath();
      for (let x = 0; x < w; x++) {
        const k = b0 + Math.floor((x / w) * (b1 - b0));
        const v = hold[Math.min(hold.length - 1, k)];
        const y = spectrumH - 2 - (v / 255) * (spectrumH - 4);
        if (x === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
      }
      ctx.stroke();
    }

    ctx.strokeStyle = "#ff5a7a";
    ctx.beginPath();
    ctx.moveTo(peakX, 0);
    ctx.lineTo(peakX, spectrumH);
    ctx.stroke();

    // waterfall
    const waterH = h - spectrumH;
    for (let y = 0; y < Math.min(WF_ROWS, waterH); y++) {
      const r = waterfall[y];
      if (!r) break;
      for (let x = 0; x < w; x++) {
        ctx.fillStyle = dbColor(r[x]);
        ctx.fillRect(x, spectrumH + y, 1, 1);
      }
    }

    setStatus(
      `peak ${peakHz.toFixed(1)} Hz · ${lo.toFixed(0)}–${hi.toFixed(0)} Hz · hold ${maxHold ? "on" : "off"}`,
      true
    );
  }

  // pinch zoom
  let pinch0 = null;
  canvas.addEventListener("touchstart", (e) => {
    if (e.touches.length === 2) {
      const dx = e.touches[0].clientX - e.touches[1].clientX;
      const dy = e.touches[0].clientY - e.touches[1].clientY;
      pinch0 = { dist: Math.hypot(dx, dy), fMin, fMax: fMax || (audioCtx ? audioCtx.sampleRate / 2 : 22050) };
      e.preventDefault();
    }
  }, { passive: false });
  canvas.addEventListener("touchmove", (e) => {
    if (e.touches.length === 2 && pinch0 && audioCtx) {
      const dx = e.touches[0].clientX - e.touches[1].clientX;
      const dy = e.touches[0].clientY - e.touches[1].clientY;
      const dist = Math.hypot(dx, dy);
      const scale = pinch0.dist / Math.max(1, dist);
      const mid = 0.5 * (pinch0.fMin + pinch0.fMax);
      let half = 0.5 * (pinch0.fMax - pinch0.fMin) * scale;
      half = Math.max(40, Math.min(audioCtx.sampleRate / 4, half));
      fMin = Math.max(0, mid - half);
      fMax = Math.min(audioCtx.sampleRate / 2, mid + half);
      e.preventDefault();
    }
  }, { passive: false });
  canvas.addEventListener("wheel", (e) => {
    if (!audioCtx) return;
    e.preventDefault();
    const rect = canvas.getBoundingClientRect();
    const t = (e.clientX - rect.left) / rect.width;
    const hi = fMax || audioCtx.sampleRate / 2;
    const anchor = fMin + t * (hi - fMin);
    let half = 0.5 * (hi - fMin) * (e.deltaY < 0 ? 0.85 : 1.15);
    half = Math.max(40, half);
    fMin = Math.max(0, anchor - 2 * half * t);
    fMax = Math.min(audioCtx.sampleRate / 2, anchor + 2 * half * (1 - t));
  }, { passive: false });

  btnStart.addEventListener("click", () => start().catch((err) => {
    setStatus(`Mic error: ${err.message}`, false);
    status.classList.add("err");
  }));
  btnStop.addEventListener("click", stop);
  btnHold.addEventListener("click", () => { maxHold = !maxHold; btnHold.textContent = maxHold ? "Max-hold ✓" : "Max-hold"; });
  btnReset.addEventListener("click", () => { if (hold) hold.fill(-999); });
  fftSel.addEventListener("change", () => { if (analyser) start().catch(console.error); });
})();
