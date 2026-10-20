/*
 * =====================================================================
 *  TUGAS AKHIR - SISTEM PEMANTAU DETAK JANTUNG LOW-COST
 *  Pulse Sensor + ESP32 + OLED SSD1306 + OTA via GitHub Releases
 * =====================================================================
 *  Pin:
 *    GPIO 34    = sinyal pulse sensor (di Wokwi: dikabel dari GPIO 25)
 *    GPIO 25    = keluaran simulator detak (HANYA untuk simulasi Wokwi,
 *                 di hardware asli pin ini tidak dipakai)
 *    GPIO 2     = LED indikator detak
 *    GPIO 21/22 = SDA/SCL OLED SSD1306 (alamat I2C 0x3C)
 *
 *  Mode:
 *    MODE_SIMULASI = true  -> dipakai saat uji di Wokwi (pulsa dibuat
 *                             otomatis oleh pin GPIO 25, atur SIM_BPM)
 *    MODE_SIMULASI = false -> dipakai di hardware asli dengan sensor
 *                             asli terpasang di GPIO 34
 * =====================================================================
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ------------------------- KONFIGURASI -------------------------------
#define PIN_SENSOR    34
#define PIN_SIM_OUT   25
#define PIN_LED       2

const bool   MODE_SIMULASI = true;   // false saat pakai sensor asli
const int    SIM_BPM       = 72;     // BPM tiruan untuk simulasi Wokwi
const int    BPM_MIN       = 60;     // batas normal bawah
const int    BPM_MAX       = 100;    // batas normal atas
const float  FIRMWARE_VERSION = 1.0; // dibandingkan dengan tag release GitHub

// Isi saat sudah siap uji OTA (untuk simulasi Wokwi tidak wajib)
const char* WIFI_SSID = "NAMA_WIFI";
const char* WIFI_PASS  = "PASSWORD_WIFI";
const char* OTA_REPO  = "akmalseptain/detak-jantung-esp32";

const unsigned long INTERVAL_CEK_OTA_MS = 6UL * 3600UL * 1000UL; // 6 jam

// ------------------------- OLED -------------------------------------
Adafruit_SSD1306 oled(128, 64, &Wire, -1);

// ------------------------- STATE DETEKSI ----------------------------
unsigned long waktuPuncakTerakhir = 0;
unsigned long ibiRiwayat[5] = {0};
byte   ibiJumlah = 0;
int    nilaiMaks = 0, nilaiMin = 4095;
bool   diAtasAmbang = false;
int    bpm = 0;
unsigned long waktuResetAmbang = 0, waktuSerial = 0, waktuOTA = 0;

// =====================================================================
//  Tugas simulasi (dijalankan di core 0, hanya saat MODE_SIMULASI):
//  membuat pulsa tiruan di GPIO 25 dengan laju SIM_BPM.
//  Di Wokwi, GPIO 25 dikabel ke GPIO 34 lewat diagram.json.
// =====================================================================
void tugasSimulasi(void*) {
  pinMode(PIN_SIM_OUT, OUTPUT);
  const unsigned long periode = 60000UL / SIM_BPM;
  for (;;) {
    digitalWrite(PIN_SIM_OUT, HIGH); delay(60);   // potongan "detak"
    digitalWrite(PIN_SIM_OUT, LOW);  delay(periode - 60);
  }
}

// =====================================================================
//  SETUP
// =====================================================================
void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT);

  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED tidak terdeteksi!");
  }
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);
  oled.println("Detak Jantung Low-Cost");
  oled.println("Mengkalibrasi...");
  oled.display();

  if (MODE_SIMULASI) {
    xTaskCreatePinnedToCore(tugasSimulasi, "sim", 2048, nullptr, 1, nullptr, 0);
    Serial.printf("[SIMULASI] Pulsa tiruan %d bpm di GPIO 25 -> GPIO 34\n", SIM_BPM);
  }

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("WiFi");
  for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500); Serial.print(".");
  }
  Serial.println(WiFi.status() == WL_CONNECTED ? " terhubung" : " gagal (mode offline)");
}

// =====================================================================
//  DETEKSI DETAK: ambang adaptif (maks/min sinyal bergerak)
// =====================================================================
void bacaSensor() {
  int adc = analogRead(PIN_SENSOR);

  // pelacak amplitudo sinyal
  if (adc > nilaiMaks) nilaiMaks = adc;
  if (adc < nilaiMin) nilaiMin = adc;

  // penurunan bertahap agar ambang menyesuaikan kalau amplitudo berubah
  unsigned long kini = millis();
  if (kini - waktuResetAmbang > 10000) {
    nilaiMaks = nilaiMin + (nilaiMaks - nilaiMin) / 2;
    waktuResetAmbang = kini;
  }
  int ambang = (nilaiMaks + nilaiMin) / 2;

  // deteksi puncak: naik melewati ambang, dengan jeda minimal 250 ms
  if (!diAtasAmbang && adc > ambang + 40 && kini - waktuPuncakTerakhir > 250) {
    diAtasAmbang = true;
    if (waktuPuncakTerakhir > 0) {
      unsigned long ibi = kini - waktuPuncakTerakhir;   // jarak antar detak
      if (ibi > 300 && ibi < 2000) {                   // 30-200 bpm wajar
        if (ibiJumlah < 5) ibiRiwayat[ibiJumlah++] = ibi;
        else { for (byte i = 0; i < 4; i++) ibiRiwayat[i] = ibiRiwayat[i + 1];
               ibiRiwayat[4] = ibi; }
        unsigned long total = 0;
        for (byte i = 0; i < ibiJumlah; i++) total += ibiRiwayat[i];
        bpm = 60000UL / (total / ibiJumlah);
      }
    }
    waktuPuncakTerakhir = kini;
    digitalWrite(PIN_LED, HIGH);
  } else if (diAtasAmbang && adc < ambang - 40) {
    diAtasAmbang = false;
    digitalWrite(PIN_LED, LOW);
  }
}

// =====================================================================
//  OTA: cek rilis terbaru di GitHub, pasang bila ada versi baru
// =====================================================================
void cekOTA() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure klien;
  klien.setInsecure();  // sebelum sidang: ganti dengan sertifikat GitHub yang disematkan
  HTTPClient http;

  String urlApi = String("https://api.github.com/repos/") + OTA_REPO + "/releases/latest";
  http.begin(klien, urlApi);
  http.addHeader("User-Agent", "detak-jantung-esp32");
  int kode = http.GET();
  if (kode != 200) { http.end(); return; }
  String isi = http.getString();
  http.end();

  // ambil tag versi, contoh: "tag_name":"v1.1"
  int iT = isi.indexOf("\"tag_name\":\"v");
  if (iT < 0) return;
  int mulai = iT + 13;
  int akhir = isi.indexOf('"', mulai);
  float versiBaru = isi.substring(mulai, akhir).toFloat();
  if (versiBaru <= FIRMWARE_VERSION) {
    Serial.printf("[OTA] Firmware v%.1f masih terbaru\n", FIRMWARE_VERSION);
    return;
  }

  // ambil URL unduhan firmware.bin
  int iU = isi.indexOf("browser_download_url\":\"https");
  if (iU < 0) return;
  mulai = iU + 24;
  akhir = isi.indexOf('"', mulai);
  String urlBin = isi.substring(mulai, akhir);
  if (!urlBin.endsWith("firmware.bin")) {
    // cari entri unduhan yang benar
    iU = isi.indexOf("browser_download_url\":\"https", akhir);
    if (iU < 0) return;
    mulai = iU + 24; akhir = isi.indexOf('"', mulai);
    urlBin = isi.substring(mulai, akhir);
  }

  Serial.printf("[OTA] Versi baru v%.1f ditemukan. Mengunduh %s\n", versiBaru, urlBin.c_str());
  http.begin(klien, urlBin);
  kode = http.GET();
  if (kode != 200) { http.end(); return; }
  int ukuran = http.getSize();
  if (ukuran <= 0 || !Update.begin(ukuran)) { http.end(); return; }
  size_t ditulis = Update.writeStream(*http.getStreamPtr());
  http.end();
  if (ditulis == (size_t)ukuran && Update.end(true)) {
    Serial.printf("[OTA] Sukses! Reboot ke v%.1f\n", versiBaru);
    delay(500);
    ESP.restart();
  } else {
    Update.abort();
    Serial.println("[OTA] GAGAL - firmware lama dipertahankan");
  }
}

// =====================================================================
//  TAMPILAN OLED + SERIAL
// =====================================================================
void tampilkan() {
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.println("Detak Jantung Low-Cost");
  oled.drawLine(0, 12, 127, 12, SSD1306_WHITE);
  oled.setTextSize(3);
  oled.setCursor(10, 26);
  if (bpm > 0) oled.print(bpm); else oled.print("--");
  oled.setTextSize(2);
  oled.print(" BPM");
  oled.setTextSize(1);
  oled.setCursor(0, 56);
  if (bpm > 0 && (bpm < BPM_MIN || bpm > BPM_MAX)) oled.print("! DI LUAR NORMAL !");
  else oled.print("Normal (60-100)");
  oled.display();

  Serial.printf("BPM: %d  | ADC maks-min: %d-%d | WiFi: %s\n",
                bpm, nilaiMaks, nilaiMin,
                WiFi.status() == WL_CONNECTED ? "OK" : "off");
}

// =====================================================================
//  LOOP UTAMA (core 1)
// =====================================================================
void loop() {
  unsigned long kini = millis();

  bacaSensor();

  if (kini - waktuSerial >= 1000) {   // tiap 1 detik
    waktuSerial = kini;
    tampilkan();
  }

  if (kini - waktuOTA >= INTERVAL_CEK_OTA_MS) {  // berkala
    waktuOTA = kini;
    cekOTA();
  }

  delay(2);  // beri napas untuk tugas inti lain
}
