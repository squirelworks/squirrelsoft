(() => {
  const cardEl = document.getElementById("card");
  const histEl = document.getElementById("history");
  const status = document.getElementById("status");
  const btnDemo = document.getElementById("btnDemo");
  const btnNfc = document.getElementById("btnNfc");
  const btnClear = document.getElementById("btnClear");

  // Shared demo payload mirrors squirrelsuica mock blocks.
  const DEMO = {
    idm: "0101020304050607",
    pmm: "0000000000000000",
    system_code: "0003",
    balance_yen: 2480,
    history: [
      { date: "2026-10-28", kind: "train", fare_yen: 168, balance_yen: 2480, note: "入場/出場" },
      { date: "2026-10-27", kind: "train", fare_yen: 210, balance_yen: 2648, note: "入場/出場" },
      { date: "2026-10-26", kind: "charge", fare_yen: 0, balance_yen: 2858, note: "チャージ" }
    ]
  };

  function render(card) {
    cardEl.textContent =
      `IDm ${card.idm}\nPMm ${card.pmm || "?"}\nSystem ${card.system_code || "0003"}\n` +
      `Balance ¥${card.balance_yen}`;
    histEl.textContent = (card.history || [])
      .map(
        (h, i) =>
          `${i + 1}. ${h.date}  ${h.kind.padEnd(6)}  fare ¥${h.fare_yen}  bal ¥${h.balance_yen}  ${h.note || ""}`
      )
      .join("\n") || "(empty)";
  }

  btnDemo.addEventListener("click", () => {
    render(DEMO);
    status.innerHTML = "<strong>Demo Suica payload loaded</strong> (no NFC hardware).";
  });

  btnClear.addEventListener("click", () => {
    cardEl.textContent = "Cleared.";
    histEl.textContent = "(empty)";
    status.textContent = "Ready.";
  });

  btnNfc.addEventListener("click", async () => {
    if (!("NDEFReader" in window)) {
      status.textContent =
        "Web NFC (NDEFReader) not available. Suica needs FeliCa/NFC-F — use squirrelsuica CLI with a PaSoRi, or the Android companion.";
      return;
    }
    try {
      const reader = new NDEFReader();
      status.textContent = "Web NFC listening… note: Suica FeliCa rarely appears as NDEF.";
      await reader.scan();
      reader.onreading = (ev) => {
        const lines = [`Serial ${ev.serialNumber || "?"}`];
        for (const rec of ev.message.records) {
          lines.push(`record type=${rec.recordType} enc=${rec.encoding || ""}`);
          if (rec.data) {
            const u8 = new Uint8Array(rec.data.buffer || rec.data);
            lines.push("  " + [...u8].map((b) => b.toString(16).padStart(2, "0")).join(" "));
          }
        }
        cardEl.textContent = lines.join("\n");
        status.textContent =
          "Got an NDEF tag. For Suica balance/history use squirrelsuica (FeliCa), not NDEF.";
      };
    } catch (err) {
      status.textContent = `Web NFC error: ${err.message}`;
    }
  });
})();
