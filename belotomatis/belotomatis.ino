/*
 * ============================================================
 *  BEL OTOMATIS - MA AL IBROHIMI MANYAR GRESIK
 * ============================================================
 *  Hardware:
 *    - NodeMCU ESP8266
 *    - RTC DS1302 (CLK=D5, DAT=D6, RST=D7)
 *    - Relay 1 Channel 5V (D1)
 *
 *  Fitur:
 *    - Web UI disajikan dari LittleFS
 *    - Sinkronisasi NTP otomatis dari device yg terkoneksi
 *    - Waktu NTP disimpan ke RTC DS1302
 *    - Jadwal bel otomatis (max 50 jadwal)
 *    - Bel manual via tombol web
 *    - Template jadwal SD/SMP/SMA
 *    - REST API JSON untuk komunikasi frontend
 *
 *  Library yang dibutuhkan:
 *    - ESP8266WiFi (built-in)
 *    - ESP8266WebServer (built-in)
 *    - LittleFS (built-in)
 *    - ArduinoJson (v7.x)
 *    - Rtc by Makuna (v2.x) -- untuk DS1302
 *
 *  Upload:
 *    1. Upload sketch ini
 *    2. Upload data/ folder via Tools > ESP8266 LittleFS Data Upload
 * ============================================================
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>
#include <DNSServer.h>
#include <ArduinoJson.h>
#include <RtcDS1302.h>
#include <ThreeWire.h>
#include <time.h>

// ─── PIN KONFIGURASI ───────────────────────────────────────
#define RELAY_PIN     D1   // Relay 1 channel (ACTIVE LOW)
#define RTC_CLK       D5
#define RTC_DAT       D6
#define RTC_RST       D7

// ─── WIFI ACCESS POINT ────────────────────────────────────
const char* AP_SSID     = "Bel-MA";
const char* AP_PASSWORD = "987654321";
const IPAddress AP_IP(192, 168, 1, 1);
const IPAddress AP_GATEWAY(192, 168, 1, 1);
const IPAddress AP_SUBNET(255, 255, 255, 0);

// ─── KONSTANTA ────────────────────────────────────────────
#define MAX_JADWAL      50
#define RELAY_ACTIVE    HIGH
#define RELAY_INACTIVE  LOW
#define CONFIG_FILE     "/config.json"
#define JADWAL_FILE     "/jadwal.json"

// ─── STRUKTUR DATA ────────────────────────────────────────
struct Jadwal {
  bool   aktif;
  uint8_t jam;
  uint8_t menit;
  char   nama[32];
  uint8_t durasi;     // detik
  bool   hari[7];     // Sen(0) Sel(1) Rab(2) Kam(3) Jum(4) Sab(5) Min(6)
};

// ─── OBJEK GLOBAL ─────────────────────────────────────────
ThreeWire      rtcWire(RTC_DAT, RTC_CLK, RTC_RST);
RtcDS1302<ThreeWire> rtc(rtcWire);
ESP8266WebServer server(80);

const byte DNS_PORT = 53;
DNSServer dnsServer;

Jadwal         jadwal[MAX_JADWAL];
uint8_t        jumlahJadwal   = 0;
bool           relayOn        = false;
unsigned long  relayEndMs     = 0;
bool           manualBel      = false;
unsigned long  manualEndMs    = 0;
int            jadwalSelesai  = 0;  // counter harian
unsigned long  lastCheckMs    = 0;
uint8_t        lastCheckMin   = 255;

// ─── FORWARD DECLARATIONS ─────────────────────────────────
void loadJadwal();
void saveJadwal();
void handleRoot();
void handleGetStatus();
void handleGetJadwal();
void handleAddJadwal();
void handleDeleteJadwal();
void handleToggleJadwal();
void handleManualBel();
void handleSetTime();
void handleTemplate();
void handleResetJadwal();
void handleStaticFile(String path, String contentType);
void ringBel(uint8_t durasiDetik);
void checkJadwal();
RtcDateTime getNow();

// ═══════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  Serial.println(F("\n\n=== BEL OTOMATIS MA AL IBROHIMI ==="));

  // Relay
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_INACTIVE);

  // LittleFS
  if (!LittleFS.begin()) {
    Serial.println(F("[FS] GAGAL mount LittleFS!"));
  } else {
    Serial.println(F("[FS] LittleFS OK"));
  }

  // RTC DS1302
  rtc.Begin();
  if (!rtc.GetIsRunning()) {
    Serial.println(F("[RTC] Tidak berjalan, menginisialisasi..."));
    rtc.SetIsRunning(true);
    RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);
    rtc.SetDateTime(compiled);
  }
  rtc.SetIsWriteProtected(false);
  Serial.println(F("[RTC] DS1302 OK"));
   
  // Muat jadwal dari file
  loadJadwal();

  // Buat WiFi Access Point
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  dnsServer.start(DNS_PORT, "*", AP_IP);
  Serial.printf("[WiFi] AP: %s  IP: %s\n", AP_SSID, AP_IP.toString().c_str());

  // ─── ROUTING ─────────────────────────────────────────────
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status",         HTTP_GET,  handleGetStatus);
  server.on("/api/jadwal",         HTTP_GET,  handleGetJadwal);
  server.on("/api/jadwal",         HTTP_POST, handleAddJadwal);
  server.on("/api/jadwal/reset",   HTTP_POST, handleResetJadwal);
  server.on("/api/jadwal/template",HTTP_POST, handleTemplate);
  server.on("/api/manual",         HTTP_POST, handleManualBel);
  server.on("/api/stop", HTTP_POST, []() {
    digitalWrite(RELAY_PIN, RELAY_INACTIVE);
    relayOn = false;
    relayEndMs = 0;
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/settime",        HTTP_POST, handleSetTime);
  server.on("/generate_204",        HTTP_GET, handleRoot);
  server.on("/hotspot-detect.html", HTTP_GET, handleRoot);
  server.on("/ncsi.txt",        HTTP_GET, handleRoot);


  // DELETE dengan ID: /api/jadwal?id=N
  server.on("/api/jadwal/delete",  HTTP_POST, handleDeleteJadwal);
  server.on("/api/jadwal/toggle",  HTTP_POST, handleToggleJadwal);

  // File statis dari LittleFS
  server.onNotFound([]() {
    String path = server.uri();
    if (LittleFS.exists(path)) {
      File f = LittleFS.open(path, "r");
      String ct = "text/plain";
      if (path.endsWith(".css"))  ct = "text/css";
      if (path.endsWith(".js"))   ct = "application/javascript";
      if (path.endsWith(".png"))  ct = "image/png";
      if (path.endsWith(".jpg"))  ct = "image/jpeg";
      if (path.endsWith(".ico"))  ct = "image/x-icon";
      if (path.endsWith(".json")) ct = "application/json";
      server.streamFile(f, ct);
      f.close();
    } else {
      server.sendHeader("Location", String("http://") + AP_IP.toString(), true);
      server.send(302, "text/plain", "");
    }
  });

  server.begin();
  Serial.println(F("[HTTP] Server started"));
  Serial.println(F("=== SIAP ===\n"));
}

// ═══════════════════════════════════════════════════════════
//  LOOP
// ═══════════════════════════════════════════════════════════
void loop() {
  dnsServer.processNextRequest();
  server.handleClient();

  unsigned long now = millis();

  // Matikan relay jika waktunya habis
  if (relayOn && now >= relayEndMs) {
    digitalWrite(RELAY_PIN, RELAY_INACTIVE);
    relayOn = false;
    Serial.println(F("[RELAY] OFF"));
  }

  // Cek jadwal setiap detik
  if (now - lastCheckMs >= 1000) {
    lastCheckMs = now;
    checkJadwal();
  }
}

// ═══════════════════════════════════════════════════════════
//  RTC HELPER
// ═══════════════════════════════════════════════════════════
RtcDateTime getNow() {
  return rtc.GetDateTime();
}

// ═══════════════════════════════════════════════════════════
//  CEK JADWAL
// ═══════════════════════════════════════════════════════════
void checkJadwal() {
  RtcDateTime dt = getNow();
  uint8_t jam   = dt.Hour();
  uint8_t menit = dt.Minute();
  uint8_t detik = dt.Second();
  uint8_t dow   = dt.DayOfWeek(); // 0=Sun,1=Mon,...,6=Sat

  // Reset counter selesai tiap tengah malam
  if (jam == 0 && menit == 0 && detik < 2) {
    jadwalSelesai = 0;
  }

  // Hanya cek 1x per menit (saat detik = 0)
  if (detik != 0) return;
  if (lastCheckMin == menit) return;
  lastCheckMin = menit;

  // Konversi dow ke index hari: Sen=0..Min=6
  // RtcDS1302: 0=Sun, 1=Mon, ..., 6=Sat
  // Kita: 0=Sen, 1=Sel, 2=Rab, 3=Kam, 4=Jum, 5=Sab, 6=Min
  uint8_t hariIdx;
  if (dow == 0)      hariIdx = 6; // Minggu
  else               hariIdx = dow - 1; // Sen=0..Sab=5

  for (uint8_t i = 0; i < jumlahJadwal; i++) {
    if (!jadwal[i].aktif) continue;
    if (jadwal[i].jam != jam)   continue;
    if (jadwal[i].menit != menit) continue;
    if (!jadwal[i].hari[hariIdx]) continue;

    Serial.printf("[BEL] Jadwal #%d: %s jam %02d:%02d\n",
                  i, jadwal[i].nama, jam, menit);
    ringBel(jadwal[i].durasi);
    jadwalSelesai++;
    break;
  }
}

// ═══════════════════════════════════════════════════════════
//  RING BEL
// ═══════════════════════════════════════════════════════════
void ringBel(uint8_t durasiDetik) {
  if (durasiDetik < 1) durasiDetik = 1;
  if (durasiDetik > 30) durasiDetik = 30;
  digitalWrite(RELAY_PIN, RELAY_ACTIVE);
  relayOn    = true;
  relayEndMs = millis() + (unsigned long)durasiDetik * 1000UL;
  Serial.printf("[RELAY] ON selama %d detik\n", durasiDetik);
}

// ═══════════════════════════════════════════════════════════
//  LOAD / SAVE JADWAL
// ═══════════════════════════════════════════════════════════
void loadJadwal() {
  jumlahJadwal = 0;
  if (!LittleFS.exists(JADWAL_FILE)) {
    Serial.println(F("[FS] jadwal.json tidak ditemukan"));
    return;
  }
  File f = LittleFS.open(JADWAL_FILE, "r");
  if (!f) return;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  
  if (err) {
    Serial.print("JSON Error: ");
    Serial.println(err.c_str());
    f.close();
    return;
  }

  JsonArray arr = doc.as<JsonArray>();
  for (JsonObject obj : arr) {
    if (jumlahJadwal >= MAX_JADWAL) break;
    Jadwal& j = jadwal[jumlahJadwal];
    j.aktif  = obj["aktif"] | true;
    j.jam    = obj["jam"]   | 0;
    j.menit  = obj["menit"] | 0;
    j.durasi = obj["durasi"]| 3;
    strlcpy(j.nama, obj["nama"] | "", sizeof(j.nama));
    JsonArray h = obj["hari"];
    for (int d = 0; d < 7; d++) j.hari[d] = h[d] | false;
    jumlahJadwal++;
  }
  Serial.printf("[FS] Loaded %d jadwal\n", jumlahJadwal);
}

void saveJadwal() {
  Serial.println("SAVE JADWAL DIPANGGIL");
  File f = LittleFS.open(JADWAL_FILE, "w");
  if (!f) {
    Serial.println("Gagal buka file");
    return;
  }
  DynamicJsonDocument doc(8192);
  JsonArray arr = doc.to<JsonArray>();
  for (uint8_t i = 0; i < jumlahJadwal; i++) {
    JsonObject obj = arr.createNestedObject();
    obj["aktif"]  = jadwal[i].aktif;
    obj["jam"]    = jadwal[i].jam;
    obj["menit"]  = jadwal[i].menit;
    obj["durasi"] = jadwal[i].durasi;
    obj["nama"]   = jadwal[i].nama;
    JsonArray h   = obj.createNestedArray("hari");
    for (int d = 0; d < 7; d++) h.add(jadwal[i].hari[d]);
  }
  serializeJson(doc, f);
  Serial.println("berhasil simpan");
  if (f) { serializeJson(doc, f); f.close(); }
}

// ═══════════════════════════════════════════════════════════
//  HTTP HANDLERS
// ═══════════════════════════════════════════════════════════

void setCORS() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void handleRoot() {
  if (LittleFS.exists("/index.html")) {
    File f = LittleFS.open("/index.html", "r");
    server.streamFile(f, "text/html");
    f.close();
  } else {
    server.send(200, "text/html",
      "<h2>LittleFS belum di-upload!</h2>"
      "<p>Upload folder <b>data/</b> via Tools &gt; ESP8266 LittleFS Data Upload</p>");
  }
}

void handleGetStatus() {
  setCORS();
  RtcDateTime dt = getNow();

  // Hitung jadwal hari ini
  uint8_t dow = dt.DayOfWeek();
  uint8_t hariIdx = (dow == 0) ? 6 : dow - 1;
  int totalHariIni = 0;
  for (uint8_t i = 0; i < jumlahJadwal; i++) {
    if (jadwal[i].aktif && jadwal[i].hari[hariIdx]) totalHariIni++;
  }
  int sisaHari = totalHariIni - jadwalSelesai;
  if (sisaHari < 0) sisaHari = 0;

  const char* namaHari[] = {"Minggu","Senin","Selasa","Rabu","Kamis","Jumat","Sabtu"};

  StaticJsonDocument<512> doc;
  doc["jam"]       = dt.Hour();
  doc["menit"]     = dt.Minute();
  doc["detik"]     = dt.Second();
  doc["hari"]      = namaHari[dt.DayOfWeek()];
  doc["tanggal"]   = dt.Day();
  doc["bulan"]     = dt.Month();
  doc["tahun"]     = dt.Year();
  doc["ip"]        = WiFi.softAPIP().toString();
  doc["jadwal"]    = totalHariIni;
  doc["selesai"]   = jadwalSelesai;
  doc["sisaHari"]  = sisaHari;
  doc["relayOn"]   = relayOn;
  doc["online"]    = true;

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleGetJadwal() {
  setCORS();
  DynamicJsonDocument doc(8192);
  JsonArray arr = doc.to<JsonArray>();
  for (uint8_t i = 0; i < jumlahJadwal; i++) {
    JsonObject obj = arr.createNestedObject();
    obj["id"]     = i;
    obj["aktif"]  = jadwal[i].aktif;
    obj["jam"]    = jadwal[i].jam;
    obj["menit"]  = jadwal[i].menit;
    obj["durasi"] = jadwal[i].durasi;
    obj["nama"]   = jadwal[i].nama;
    JsonArray h   = obj.createNestedArray("hari");
    for (int d = 0; d < 7; d++) h.add(jadwal[i].hari[d]);
  }
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleAddJadwal() {
  setCORS();
  if (jumlahJadwal >= MAX_JADWAL) {
    server.send(400, "application/json", "{\"error\":\"Jadwal penuh (max 50)\"}");
    return;
  }
  DynamicJsonDocument doc(512);
  if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok) {
    server.send(400, "application/json", "{\"error\":\"JSON invalid\"}");
    return;
  }
  Jadwal& j = jadwal[jumlahJadwal];
  j.aktif  = doc["aktif"] | true;
  j.jam    = doc["jam"]   | 0;
  j.menit  = doc["menit"] | 0;
  j.durasi = doc["durasi"]| 3;
  strlcpy(j.nama, doc["nama"] | "Bel", sizeof(j.nama));
  JsonArray h = doc["hari"];
  for (int d = 0; d < 7; d++) j.hari[d] = h[d] | false;
  jumlahJadwal++;
  saveJadwal();
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleDeleteJadwal() {
  setCORS();
  DynamicJsonDocument doc(64);
  deserializeJson(doc, server.arg("plain"));
  int id = doc["id"] | -1;
  if (id < 0 || id >= jumlahJadwal) {
    server.send(400, "application/json", "{\"error\":\"ID tidak valid\"}");
    return;
  }
  for (int i = id; i < jumlahJadwal - 1; i++) jadwal[i] = jadwal[i + 1];
  jumlahJadwal--;
  saveJadwal();
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleToggleJadwal() {
  setCORS();
  DynamicJsonDocument doc(64);
  deserializeJson(doc, server.arg("plain"));
  int id = doc["id"] | -1;
  if (id < 0 || id >= jumlahJadwal) {
    server.send(400, "application/json", "{\"error\":\"ID tidak valid\"}");
    return;
  }
  jadwal[id].aktif = !jadwal[id].aktif;
  saveJadwal();
  server.send(200, "application/json", "{\"ok\":true,\"aktif\":jadwal[id].aktif}");
}

void handleManualBel() {
  setCORS();
  DynamicJsonDocument doc(64);
  deserializeJson(doc, server.arg("plain"));
  uint8_t dur = doc["durasi"] | 3;
  ringBel(dur);
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleSetTime() {
  setCORS();
  DynamicJsonDocument doc(128);
  if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok) {
    server.send(400, "application/json", "{\"error\":\"JSON invalid\"}");
    return;
  }
  // Format: {"tahun":2024,"bulan":5,"hari":24,"jam":16,"menit":42,"detik":0}
  uint16_t tahun  = doc["tahun"]  | 2024;
  uint8_t  bulan  = doc["bulan"]  | 1;
  uint8_t  hari   = doc["hari"]   | 1;
  uint8_t  jam    = doc["jam"]    | 0;
  uint8_t  menit  = doc["menit"]  | 0;
  uint8_t  detik  = doc["detik"]  | 0;

  RtcDateTime newTime(tahun, bulan, hari, jam, menit, detik);
  rtc.SetDateTime(newTime);
  Serial.printf("[RTC] Set waktu: %04d-%02d-%02d %02d:%02d:%02d\n",
                tahun, bulan, hari, jam, menit, detik);
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleResetJadwal() {
  setCORS();
  jumlahJadwal = 0;
  saveJadwal();
  jadwalSelesai = 0;
  server.send(200, "application/json", "{\"ok\":true}");
}

// Template jadwal
struct TemplateEntry { uint8_t jam; uint8_t menit; const char* nama; uint8_t dur; };

const TemplateEntry TMPL_KBM[] = {
  {6,45,"DOA & APEL",5},{7,0,"Jam 1",3},{7,40,"Jam 2",3},
  {8,20,"Jam 3",3},{09,0,"ISTIRAHAT",3},{10,0,"Jam 5",3},
  {10,40,"Jam 6",3},{11,20,"Jam 7",3},{12,0,"Jam 8",3}
};
const TemplateEntry TMPL_ASAT[] = {
  {7,15,"Persiapan Ujian",5},{7,30,"Mapel Ujian I",3},{9,0,"Istirahat",3},
  {9,30,"Masuk Kelas",3},{11,0,"Mapel Ujian II",3}
};
const TemplateEntry TMPL_MA[] = {
  {6,45,"Masuk Sekolah",5},{7,0,"Mulai Pelajaran",3},{9,30,"Istirahat",5},
  {9,45,"Masuk Kelas",3},{12,0,"Sholat Dzuhur",5},{12,30,"Masuk Kelas",3},
  {15,0,"Pulang",10},{15,30,"Ekstra Kurikuler",5}
};

void handleTemplate() {
  setCORS();
  DynamicJsonDocument doc(64);
  deserializeJson(doc, server.arg("plain"));
  String tipe = doc["tipe"] | "KBM";

  const TemplateEntry* tmpl = nullptr;
  int sz = 0;
  if (tipe == "KBM")  { tmpl = TMPL_KBM;  sz = 7; }
  if (tipe == "ASAT") { tmpl = TMPL_ASAT; sz = 7; }
  if (tipe == "MA") { tmpl = TMPL_MA; sz = 8; }

  if (!tmpl) { server.send(400, "application/json", "{\"error\":\"Tipe tidak valid\"}"); return; }

  // Hari kerja: Sen-Jum (index 0-4)
  for (int i = 0; i < sz && jumlahJadwal < MAX_JADWAL; i++) {
    Jadwal& j = jadwal[jumlahJadwal];
    j.aktif  = true;
    j.jam    = tmpl[i].jam;
    j.menit  = tmpl[i].menit;
    j.durasi = tmpl[i].dur;
    strlcpy(j.nama, tmpl[i].nama, sizeof(j.nama));
    for (int d = 0; d < 7; d++) j.hari[d] = (d < 5); // Sen-Jum
    jumlahJadwal++;
  }
  saveJadwal();
  server.send(200, "application/json", "{\"ok\":true}");
}
