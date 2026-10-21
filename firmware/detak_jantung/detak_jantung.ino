/*
 * =====================================================================
 *  TUGAS AKHIR - SISTEM PEMANTAU DETAK JANTUNG LOW-COST
 *  Pulse Sensor + ESP32 + OLED SSD1306 + OTA via GitHub Releases
 *
 *  Metode pengukuran disamakan dengan simulasi Proteus dan repo
 *  Heart_Beat_Simulation_Proteus: hitung pulsa pada jendela 6 detik,
 *  lalu BPM = jumlah detak x 10. Jendela diulang otomatis supaya alat
 *  tetap memantau secara terus-menerus.
 * =====================================================================
 *  Pin:
 *    GPIO 34    = sinyal pulse sensor (di Wokwi: dikabel dari GPIO 25)
 *    GPIO 25    = keluaran simulator detak (HANYA untuk simulasi Wokwi,
 *                 di hardware asli pin ini tidak dipakai)
 *    GPIO 0     = tombol BOOT, untuk memulai ulang jendela pengukuran
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
#define PIN_TOMBOL     0
#define PIN_LED        2

const bool   MODE_SIMULASI    = true;   // false saat pakai sensor asli
const int    SIM_BPM          = 72;     // BPM tiruan untuk simulasi Wokwi
const int    BPM_MIN          = 60;     // batas normal bawah
const int    BPM_MAX          = 100;    // batas normal atas
const float  FIRMWARE_VERSION = 1.1;    // dibandingkan dengan tag release GitHub

const uint8_t DURASI_DETIK = 6;         // jendela pengukuran (detik)

// Isi saat sudah siap uji OTA (untuk simulasi Wokwi tidak wajib)
const char* WIFI_SSID = "NAMA_WIFI";
const char* WIFI_PASS  = "PASSWORD_WIFI";
const char* OTA_REPO  = "akmalseptain/-detak-jantung-esp32";

const unsigned long INTERVAL_CEK_OTA_MS = 6UL * 3600UL * 1000UL; // 6 jam

// ------------------------- OLED -------------------------------------
Adafruit_SSD1306 oled(128, 64, &Wire, -1);

// ------------------------- STATE DETEKSI ----------------------------
unsigned long waktuPuncakTerakhir = 0;
int    nilaiMaks = 0, nilaiMin = 4095;
bool   diAtasAmbang = false;

uint32_t jumlahDetak = 0;       // detak pada jendela berjalan
uint8_t  detikJendela = 0;      // detik berjalan dalam jendela
int      bpm = 0;               // hasil jendela terakhir

unsigned long waktuDetik = 0, waktuSerial = 0, waktuOTA = 0, waktuLed = 0;

// =====================================================================
//  Tugas simulasi (core 0, hanya saat MODE_SIMULASI):
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
  pinMode(PIN_TOMBOL, INPUT_PULLUP);

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

  waktuDetik = millis();
}

// =====================================================================
//  DETEKSI DETAK: ambang adaptif (maks/min sinyal bergerak)
//  Mengembalikan true tepat satu kali untuk setiap tepi naik.
// =====================================================================
bool deteksiDetak(unsigned long kini) {
  int adc = analogRead(PIN_SENSOR);

  // pelacak amplitudo sinyal
  if (adc > nilaiMaks) nilaiMaks = adc;
  if (adc < nilaiMin) nilaiMin = adc;

  // penurunan bertahap agar ambang menyesuaikan kalau amplitudo berubah
  static unsigned long waktuResetAmbang = 0;
  if (kini - waktuResetAmbang > 10000) {
    nilaiMaks = nilaiMin + (nilaiMaks - nilaiMin) / 2;
    waktuResetAmbang = kini;
  }
  int ambang = (nilaiMaks + nilaiMin) / 2;

  bool detak = false;
  if (!diAtasAmbang && adc > ambang + 40 && kini - waktuPuncakTerakhir > 250) {
    diAtasAmbang = true;
    waktuPuncakTerakhir = kini;
    detak = true;
  } else if (diAtasAmbang && adc < ambang - 40) {
    diAtasAmbang = false;
  }
  return detak;
}

// =====================================================================
//  TOMBOL: memulai ulang jendela pengukuran (debounce 50 ms)
// =====================================================================
bool tombolDitekan() {
  static bool stabil = HIGH, terakhir = HIGH;
  static unsigned long waktu = 0;

  bool baca = digitalRead(PIN_TOMBOL);
  if (baca != terakhir) {
    terakhir = baca;
    waktu = millis();
  }
  if (millis() - waktu > 50 && baca != stabil) {
    stabil = baca;
    if (stabil == LOW) return true;
  }
  return false;
}

// =====================================================================
//  OTA: cek rilis terbaru di GitHub, pasang bila ada versi baru
// =====================================================================
String ambilNilaiJson(const String& json, const String& kunci) {
  int i = json.indexOf(kunci);
  if (i < 0) return "";
  i += kunci.length();
  int akhir = json.indexOf('"', i);
  if (akhir < 0) return "";
  return json.substring(i, akhir);
}

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
  float versiBaru = ambilNilaiJson(isi, "\"tag_name\":\"v\"").toFloat();
  if (versiBaru <= FIRMWARE_VERSION) {
    Serial.printf("[OTA] Firmware v%.1f masih terbaru\n", FIRMWARE_VERSION);
    return;
  }

  // cari aset bernama firmware.bin, lalu ambil URL unduhannya
  int iAset = isi.indexOf("\"name\":\"firmware.bin\"");
  if (iAset < 0) {
    Serial.println("[OTA] Aset firmware.bin tidak ditemukan");
    return;
  }
  int iU = isi.indexOf("browser_download_url\":\"", iAset);
  if (iU < 0) return;
  int mulai = iU + 24;
  int akhir = isi.indexOf('"', mulai);
  String urlBin = isi.substring(mulai, akhir);

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
  oled.setCursor(10, 20);
  if (bpm > 0) oled.print(bpm); else oled.print("--");
  oled.setTextSize(2);
  oled.print(" BPM");

  oled.setTextSize(1);
  oled.setCursor(0, 44);
  if (bpm == 0) {
    oled.printf("Mengukur %u/%u s", (unsigned)detikJendela, (unsigned)DURASI_DETIK);
  } else if (bpm < BPM_MIN || bpm > BPM_MAX) {
    oled.printf("! DI LUAR NORMAL ! %u/%u", (unsigned)detikJendela, (unsigned)DURASI_DETIK);
  } else {
    oled.printf("Normal (60-100) %u/%u s", (unsigned)detikJendela, (unsigned)DURASI_DETIK);
  }

  oled.setCursor(0, 56);
  oled.print(WiFi.status() == WL_CONNECTED ? "WiFi: OK" : "WiFi: off");
  oled.display();

  Serial.printf("BPM: %d | detak: %lu | jendela: %u/%u s | WiFi: %s\n",
                bpm, (unsigned long)jumlahDetak,
                (unsigned)detikJendela, (unsigned)DURASI_DETIK,
                WiFi.status() == WL_CONNECTED ? "OK" : "off");
}

// =====================================================================
//  LOOP UTAMA (core 1)
// =====================================================================
void loop() {
  unsigned long kini = millis();

  if (deteksiDetak(kini)) {
    jumlahDetak++;
    digitalWrite(PIN_LED, HIGH);
    waktuLed = kini;
  }
  if (digitalRead(PIN_LED) == HIGH && kini - waktuLed > 100) {
    digitalWrite(PIN_LED, LOW);
  }

  // penanda 1 detik -> majukan jendela pengukuran
  if (kini - waktuDetik >= 1000) {
    waktuDetik += 1000;
    detikJendela++;

    if (detikJendela >= DURASI_DETIK) {
      bpm = (int)(jumlahDetak * (60UL / DURASI_DETIK));  // 6 detik -> x10
      Serial.printf("[JENDELA] %lu detak / %u detik -> %d BPM\n",
                    (unsigned long)jumlahDetak, (unsigned)DURASI_DETIK, bpm);
      jumlahDetak = 0;
      detikJendela = 0;
    }
    tampilkan();
  }

  // tombol BOOT: mulai ulang jendela tanpa menunggu selesai
  if (tombolDitekan()) {
    Serial.println("[TOMBOL] Jendela pengukuran dimulai ulang");
    jumlahDetak = 0;
    detikJendela = 0;
    bpm = 0;
    waktuDetik = kini;
    tampilkan();
  }

  if (kini - waktuOTA >= INTERVAL_CEK_OTA_MS) {  // berkala
    waktuOTA = kini;
    cekOTA();
  }

  delay(2);  // beri napas untuk tugas inti lain
}
