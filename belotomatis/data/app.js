/* ═══════════════════════════════════════════════
   BEL OTOMATIS – MA AL IBROHIMI
   app.js – Frontend Logic
═══════════════════════════════════════════════ */

"use strict";

// ─── DRUM PICKER ──────────────────────────────
const drumState = { jam: 7, menit: 0 };

function createDrum(elId, items, initial, onChange) {
  const el = document.getElementById(elId);
  const ITEM_H = 40;

  // fade overlay
  el.insertAdjacentHTML(
    "beforeend",
    '<div class="drum-fade-top"></div><div class="drum-fade-bot"></div>',
  );

  // track
  const track = document.createElement("div");
  track.className = "drum-track";

  // 2 dummy atas + items + 2 dummy bawah agar tengah bisa dipilih
  const pad = 2;
  for (let p = 0; p < pad; p++) {
    const d = document.createElement("div");
    d.className = "drum-item";
    track.appendChild(d);
  }
  items.forEach((v, i) => {
    const d = document.createElement("div");
    d.className = "drum-item";
    d.textContent = String(v).padStart(2, "0");
    d.dataset.idx = i;
    track.appendChild(d);
  });
  for (let p = 0; p < pad; p++) {
    const d = document.createElement("div");
    d.className = "drum-item";
    track.appendChild(d);
  }
  el.appendChild(track);

  let curIdx = items.indexOf(initial) >= 0 ? items.indexOf(initial) : 0;
  let startY = 0;
  let startOff = 0;
  let isDrag = false;
  let offset = 0;

  function clamp(idx) {
    return Math.max(0, Math.min(items.length - 1, idx));
  }

  function snapTo(idx, animate = true) {
    curIdx = clamp(idx);
    offset = -curIdx * ITEM_H;
    track.style.transition = animate
      ? "transform 0.2s cubic-bezier(0.25,0.46,0.45,0.94)"
      : "none";
    track.style.transform = `translateY(${offset}px)`;
    updateHighlight();
    onChange(items[curIdx]);
    if (navigator.vibrate) navigator.vibrate(10);
  }

  function updateHighlight() {
    track.querySelectorAll(".drum-item").forEach((d, i) => {
      const dataIdx = parseInt(d.dataset.idx);
      if (isNaN(dataIdx)) {
        d.className = "drum-item";
        return;
      }
      const dist = Math.abs(dataIdx - curIdx);
      if (dist === 0) d.className = "drum-item selected";
      else if (dist === 1) d.className = "drum-item near";
      else d.className = "drum-item";
    });
  }

  function getY(e) {
    return e.touches ? e.touches[0].clientY : e.clientY;
  }

  el.addEventListener("mousedown", onStart, { passive: false });
  el.addEventListener("touchstart", onStart, { passive: false });

  function onStart(e) {
    e.preventDefault();
    isDrag = true;
    startY = getY(e);
    startOff = offset;
    track.style.transition = "none";
    document.addEventListener("mousemove", onMove, { passive: false });
    document.addEventListener("touchmove", onMove, { passive: false });
    document.addEventListener("mouseup", onEnd);
    document.addEventListener("touchend", onEnd);
  }

  function onMove(e) {
    if (!isDrag) return;
    e.preventDefault();
    const dy = getY(e) - startY;
    offset = startOff + dy;
    track.style.transform = `translateY(${offset}px)`;
    const nearIdx = clamp(Math.round(-offset / ITEM_H));
    updateHighlightTemp(nearIdx);
  }

  function updateHighlightTemp(nearIdx) {
    track.querySelectorAll(".drum-item").forEach((d) => {
      const dataIdx = parseInt(d.dataset.idx);
      if (isNaN(dataIdx)) {
        d.className = "drum-item";
        return;
      }
      const dist = Math.abs(dataIdx - nearIdx);
      if (dist === 0) d.className = "drum-item selected";
      else if (dist === 1) d.className = "drum-item near";
      else d.className = "drum-item";
    });
  }

  function onEnd() {
    isDrag = false;
    document.removeEventListener("mousemove", onMove);
    document.removeEventListener("touchmove", onMove);
    document.removeEventListener("mouseup", onEnd);
    document.removeEventListener("touchend", onEnd);
    const newIdx = clamp(Math.round(-offset / ITEM_H));
    snapTo(newIdx);
  }

  // wheel scroll
  el.addEventListener(
    "wheel",
    (e) => {
      e.preventDefault();
      snapTo(clamp(curIdx + (e.deltaY > 0 ? 1 : -1)));
    },
    { passive: false },
  );

  snapTo(curIdx, false);
}

// ─── STATE ────────────────────────────────────
let selDur = 3;
let manualTimer = null;
let pollTimer = null;

// ─── INIT ─────────────────────────────────────
document.addEventListener("DOMContentLoaded", () => {
  // Init drum picker
  const jamItems = Array.from({ length: 24 }, (_, i) => i);
  const menitItems = Array.from({ length: 12 }, (_, i) => i * 5);
  createDrum("drumJam", jamItems, 7, (v) => {
    drumState.jam = v;
  });
  createDrum("drumMenit", menitItems, 0, (v) => {
    drumState.menit = v;
  });

  setupHariButtons();
  autoSyncNTP();
  setInterval(autoSyncNTP, 60 * 60 * 1000);
  refreshStatus();
  refreshJadwal();
  pollTimer = setInterval(refreshStatus, 1000);
});

// ─── STATUS POLL ──────────────────────────────
async function refreshStatus() {
  try {
    const r = await fetch("/api/status");
    if (!r.ok) throw new Error("fetch fail");
    const d = await r.json();

    const jam = String(d.jam).padStart(2, "0");
    const menit = String(d.menit).padStart(2, "0");
    const detik = String(d.detik).padStart(2, "0");

    document.getElementById("jamTeks").textContent = jam;
    document.getElementById("menitTeks").textContent = menit;
    document.getElementById("detikTeks").textContent = detik;

    const bulanNama = [
      "Jan",
      "Feb",
      "Mar",
      "Apr",
      "Mei",
      "Jun",
      "Jul",
      "Agu",
      "Sep",
      "Okt",
      "Nov",
      "Des",
    ];
    document.getElementById("hariTeks").textContent = d.hari;
    document.getElementById("tanggalTeks").textContent =
      d.tanggal + " " + bulanNama[d.bulan - 1];
    document.getElementById("ipTeks").textContent = d.ip;

    document.getElementById("statJadwal").textContent = d.jadwal;
    document.getElementById("statSelesai").textContent = d.selesai;
    document.getElementById("statSisa").textContent = d.sisaHari;

    const badge = document.getElementById("badgeOnline");
    if (d.online) {
      badge.textContent = "● ONLINE";
      badge.style.color = "var(--green)";
    }

    // Visual relay feedback
    if (d.relayOn) {
      document.body.classList.add("relay-active");
    } else {
      document.body.classList.remove("relay-active");
    }
  } catch (e) {
    const badge = document.getElementById("badgeOnline");
    badge.textContent = "● OFFLINE";
    badge.style.color = "var(--red)";
  }
}

// ─── JADWAL ───────────────────────────────────
async function refreshJadwal() {
  try {
    const r = await fetch("/api/jadwal");
    const list = await r.json();
    renderJadwal(list);
    document.getElementById("jadwalCount").textContent = list.length + " / 50";
  } catch (e) {
    console.error("Gagal load jadwal", e);
  }
}

const NAMA_HARI = ["Sen", "Sel", "Rab", "Kam", "Jum", "Sab", "Min"];

function renderJadwal(list) {
  const container = document.getElementById("jadwalList");
  if (!list || list.length === 0) {
    container.innerHTML = `
      <div class="empty-state">
        <div class="empty-bell">🔔</div>
        <div>Belum ada jadwal.</div>
        <div>Tambah manual atau pilih template.</div>
      </div>`;
    return;
  }

  // Sort by jam:menit
  list.sort((a, b) => a.jam * 60 + a.menit - (b.jam * 60 + b.menit));

  container.innerHTML = list
    .map((j) => {
      const timeStr =
        String(j.jam).padStart(2, "0") + ":" + String(j.menit).padStart(2, "0");
      const hariBadges = j.hari
        .map((on, i) => (on ? `<span class="hb">${NAMA_HARI[i]}</span>` : ""))
        .join("");
      const activeClass = j.aktif ? "active-border" : "inactive";

      return `
    <div class="jadwal-item ${activeClass}" id="ji-${j.id}">
      <div class="jadwal-time">${timeStr}</div>
      <div class="jadwal-info">
        <div class="jadwal-nama">${escHtml(j.nama) || "—"}</div>
        <div class="jadwal-meta">
          <span class="jadwal-dur">${j.durasi}s</span>
          <span class="jadwal-hari-badges">${hariBadges}</span>
        </div>
      </div>
      <div class="jadwal-actions">
        <button class="btn-toggle" onclick="toggleJadwal(${j.id})" title="${j.aktif ? "Nonaktifkan" : "Aktifkan"}">
          ${j.aktif ? "●" : "○"}
        </button>
        <button class="btn-del" onclick="deleteJadwal(${j.id})" title="Hapus">✕</button>
      </div>
    </div>`;
    })
    .join("");
}

// ─── ADD JADWAL ───────────────────────────────
async function addJadwal() {
  const jam = drumState.jam;
  const menit = drumState.menit;
  const nama = document.getElementById("inpNama").value.trim() || "Bel";
  const hari = getHariSelected();

  if (!hari.some((h) => h)) {
    toast("Pilih minimal 1 hari aktif!", "error");
    return;
  }

  const payload = { jam, menit, nama, durasi: selDur, hari, aktif: true };
  try {
    const r = await fetch("/api/jadwal", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload),
    });
    const d = await r.json();
    if (d.ok) {
      toast("Jadwal ditambahkan!", "success");
      document.getElementById("inpNama").value = "";
      refreshJadwal();
    } else {
      toast(d.error || "Gagal menambah", "error");
    }
  } catch (e) {
    toast("Koneksi gagal!", "error");
  }
}

async function deleteJadwal(id) {
  if (!confirm("Hapus jadwal ini?")) return;
  try {
    const r = await fetch("/api/jadwal/delete", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ id }),
    });
    const d = await r.json();
    if (d.ok) {
      toast("Jadwal dihapus", "success");
      refreshJadwal();
    } else toast(d.error || "Gagal hapus", "error");
  } catch (e) {
    toast("Koneksi gagal!", "error");
  }
}

async function toggleJadwal(id) {
  try {
    const r = await fetch("/api/jadwal/toggle", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ id }),
    });
    const d = await r.json();
    if (d.ok) refreshJadwal();
  } catch (e) {
    toast("Koneksi gagal!", "error");
  }
}

async function resetJadwal() {
  if (!confirm("Reset semua jadwal? Tindakan ini tidak dapat dibatalkan."))
    return;
  try {
    const r = await fetch("/api/jadwal/reset", { method: "POST" });
    const d = await r.json();
    if (d.ok) {
      toast("Semua jadwal dihapus", "success");
      refreshJadwal();
      refreshStatus();
    }
  } catch (e) {
    toast("Koneksi gagal!", "error");
  }
}

// ─── TEMPLATE ─────────────────────────────────
async function applyTemplate(tipe) {
  if (
    !confirm(`Tambahkan template jadwal ${tipe}?\n(Jadwal existing tetap ada)`)
  )
    return;
  try {
    const r = await fetch("/api/jadwal/template", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ tipe }),
    });
    const d = await r.json();
    if (d.ok) {
      toast(`Template ${tipe} diterapkan!`, "success");
      refreshJadwal();
      refreshStatus();
    } else toast(d.error || "Gagal template", "error");
  } catch (e) {
    toast("Koneksi gagal!", "error");
  }
}

// ─── MANUAL BEL ───────────────────────────────
function startManual() {
  const btn = document.getElementById("bellBtn");
  btn.classList.add("ringing");
  document.getElementById("bellLabel").textContent = "MEMBUNYIKAN...";

  // Getar HP: pola 500ms getar, 200ms jeda, berulang terus
  if (navigator.vibrate) {
    navigator.vibrate([
      500, 200, 500, 200, 500, 200, 500, 200, 500, 200, 500, 200, 500, 200, 500,
      200, 500, 200, 500, 200, 500, 200, 500, 200, 500, 200, 500, 200, 500, 200,
    ]);
  }

  fetch("/api/manual", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ durasi: 30 }),
  }).catch(() => {});

  // Kirim ulang tiap 25 detik selama dipegang
  manualTimer = setInterval(() => {
    fetch("/api/manual", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ durasi: 30 }),
    }).catch(() => {});
  }, 25000);
}

function stopManual() {
  clearInterval(manualTimer);
  manualTimer = null;

  // Hentikan getaran
  if (navigator.vibrate) navigator.vibrate(0);

  const btn = document.getElementById("bellBtn");
  btn.classList.remove("ringing");
  document.getElementById("bellLabel").textContent = "TAHAN BEL";

  fetch("/api/stop", { method: "POST" }).catch(() => {});
}

// ─── AUTO NTP SYNC ────────────────────────────
// Otomatis kirim waktu dari browser ke RTC saat halaman dibuka
// dan setiap 1 jam sekali selama halaman terbuka
async function autoSyncNTP() {
  const now = new Date();
  const payload = {
    tahun: now.getFullYear(),
    bulan: now.getMonth() + 1,
    hari: now.getDate(),
    jam: now.getHours(),
    menit: now.getMinutes(),
    detik: now.getSeconds(),
  };
  try {
    await fetch("/api/settime", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload),
    });
    console.log("[NTP] Waktu otomatis disinkron ke RTC:", now.toLocaleString());
  } catch (e) {
    console.warn("[NTP] Gagal sync:", e);
  }
}

// ─── UI HELPERS ───────────────────────────────
function setDur(v) {
  selDur = v;
  document.querySelectorAll(".dur-btn").forEach((b) => {
    b.classList.toggle("active", parseInt(b.textContent) === v);
  });
}

function setupHariButtons() {
  document.querySelectorAll(".hari-btn").forEach((b) => {
    b.addEventListener("click", () => {
      b.classList.toggle("active");
    });
  });
}

function getHariSelected() {
  const btns = document.querySelectorAll(".hari-btn");
  return Array.from(btns).map((b) => b.classList.contains("active"));
}

function escHtml(str) {
  return str.replace(
    /[&<>"']/g,
    (m) =>
      ({
        "&": "&amp;",
        "<": "&lt;",
        ">": "&gt;",
        '"': "&quot;",
        "'": "&#039;",
      })[m],
  );
}

// ─── TOAST ────────────────────────────────────
let toastTimer = null;
function toast(msg, type = "") {
  let el = document.getElementById("toast");
  if (!el) {
    el = document.createElement("div");
    el.id = "toast";
    document.body.appendChild(el);
  }
  el.textContent = msg;
  el.className = "show " + type;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => {
    el.className = el.className.replace("show", "").trim();
  }, 2500);
}

// CSS keyframe for spin (injected)
const style = document.createElement("style");
style.textContent = "@keyframes spin { to { transform: rotate(360deg); } }";
document.head.appendChild(style);
