(() => {
  const video = document.getElementById("video");
  const overlay = document.getElementById("overlay");
  const octx = overlay.getContext("2d");
  const resultEl = document.getElementById("result");
  const status = document.getElementById("status");
  const btnStart = document.getElementById("btnStart");
  const btnStop = document.getElementById("btnStop");
  const btnCopy = document.getElementById("btnCopy");
  const facing = document.getElementById("facing");
  const file = document.getElementById("file");

  let stream = null;
  let detector = null;
  let raf = 0;
  let lastText = "";

  function setStatus(msg) { status.textContent = msg; }

  async function ensureDetector() {
    if (detector) return detector;
    if (!("BarcodeDetector" in window)) {
      throw new Error(
        "BarcodeDetector unsupported in this browser. Use Chrome/Edge on Android, or scan from an image file on a supporting browser."
      );
    }
    const formats = ["qr_code", "aztec", "data_matrix", "pdf417", "ean_13", "code_128"];
    let supported = formats;
    if (BarcodeDetector.getSupportedFormats) {
      const got = await BarcodeDetector.getSupportedFormats();
      supported = formats.filter((f) => got.includes(f));
      if (!supported.length) supported = ["qr_code"];
    }
    detector = new BarcodeDetector({ formats: supported });
    return detector;
  }

  function syncOverlaySize() {
    const rect = video.getBoundingClientRect();
    overlay.width = video.videoWidth || rect.width;
    overlay.height = video.videoHeight || rect.height;
    overlay.style.width = "100%";
    overlay.style.height = "100%";
  }

  function drawBoxes(codes) {
    octx.clearRect(0, 0, overlay.width, overlay.height);
    octx.strokeStyle = "#5dffa8";
    octx.lineWidth = 3;
    for (const c of codes) {
      if (!c.boundingBox) continue;
      const { x, y, width, height } = c.boundingBox;
      octx.strokeRect(x, y, width, height);
    }
  }

  function publish(text, fmt) {
    if (!text) return;
    lastText = text;
    resultEl.textContent = `[${fmt || "code"}] ${text}`;
    btnCopy.disabled = false;
    setStatus(`Detected ${fmt || "code"}`);
    if (navigator.vibrate) navigator.vibrate(30);
  }

  async function detectFromSource(source) {
    const det = await ensureDetector();
    const codes = await det.detect(source);
    drawBoxes(codes);
    if (codes && codes.length) {
      publish(codes[0].rawValue, codes[0].format);
      return true;
    }
    return false;
  }

  async function loop() {
    raf = requestAnimationFrame(loop);
    if (!video.videoWidth) return;
    syncOverlaySize();
    try {
      await detectFromSource(video);
    } catch (err) {
      setStatus(`Detect error: ${err.message}`);
    }
  }

  async function start() {
    stop();
    await ensureDetector();
    stream = await navigator.mediaDevices.getUserMedia({
      audio: false,
      video: {
        facingMode: { ideal: facing.value },
        width: { ideal: 1280 },
        height: { ideal: 720 }
      }
    });
    video.srcObject = stream;
    await video.play();
    btnStart.disabled = true;
    btnStop.disabled = false;
    setStatus("Camera running — point at a QR code.");
    loop();
  }

  function stop() {
    cancelAnimationFrame(raf);
    if (stream) stream.getTracks().forEach((t) => t.stop());
    stream = null;
    video.srcObject = null;
    octx.clearRect(0, 0, overlay.width, overlay.height);
    btnStart.disabled = false;
    btnStop.disabled = true;
    setStatus("Stopped.");
  }

  btnStart.addEventListener("click", () => {
    start().catch((err) => setStatus(`Camera error: ${err.message}`));
  });
  btnStop.addEventListener("click", stop);
  btnCopy.addEventListener("click", async () => {
    if (!lastText) return;
    await navigator.clipboard.writeText(lastText);
    setStatus("Copied to clipboard.");
  });
  file.addEventListener("change", async () => {
    const f = file.files && file.files[0];
    if (!f) return;
    const url = URL.createObjectURL(f);
    const img = new Image();
    img.onload = async () => {
      try {
        const ok = await detectFromSource(img);
        if (!ok) setStatus("No code found in image.");
      } catch (err) {
        setStatus(err.message);
      }
      URL.revokeObjectURL(url);
    };
    img.src = url;
  });
})();
