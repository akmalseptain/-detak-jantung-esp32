/*
 * =====================================================================
 *  TUGAS AKHIR - SIMULASI PROTEUS (Arduino Uno / ATmega328P)
 *  Alat Pemantau Detak Jantung Low-Cost - logika deteksi BPM
 * =====================================================================
 *  Rangkaian di Proteus:
 *    1. Arduino Uno (ATmega328P)
 *    2. Signal Generator / DC Pulse -> hubungkan ke A0
 *       (pengganti pulse sensor; set frekuensi sesuai skenario:
 *        60 / 80 / 100 / 120 bpm -> 1 / 1.33 / 1.67 / 2 Hz)
 *    3. LED indikator detak di pin 13 (on-board Uno)
 *    4. Virtual Terminal di pin TX (serial monitor, 9600 baud)
 *    5. OscilloscopeProbe pada A0 untuk melihat sinyal
 *
 *  Cara pakai:
 *    - Compile sketch ini di Arduino IDE, ambil file .hex
 *      (Sketch -> Export Compiled Binary, atau aktifkan verbose build)
 *    - Klik dua kali komponen Uno di Proteus -> Program File: pilih .hex
 *    - Jalankan simulasi
 *
 *  Logika deteksi puncak SAMA dengan firmware ESP32 di
 *  firmware/detak_jantung/detak_jantung.ino - hanya platform berbeda.
 * =====================================================================
 */

#define PIN_SENSOR A0    // input sinyal pulse (pengganti sensor di simulasi)
#define PIN_LED    13    // LED indikator detak

const int BPM_MIN = 60;  // batas normal bawah
const int BPM_MAX = 100; // batas normal atas

// ---------- STATE DETEKSI ----------
unsigned long waktuPuncakTerakhir = 0;
unsigned long ibiRiwayat[5] = {0};
byte   ibiJumlah = 0;
int    nilaiMaks = 0, nilaiMin = 1023;
bool   diAtasAmbang = false;
int    bpm = 0;
unsigned long waktuResetAmbang = 0, waktuSerial = 0;

void setup() {
  Serial.begin(9600);
  pinMode(PIN_LED, OUTPUT);
  Serial.println("=== Simulasi Detak Jantung - Proteus ===");
  Serial.println("Hubungkan signal generator ke A0");
  Serial.println("Skenario: 60/80/100/120 bpm");
}

void loop() {
  unsigned long kini = millis();
  int adc = analogRead(PIN_SENSOR);

  // pelacak amplitudo sinyal (ambang adaptif)
  if (adc > nilaiMaks) nilaiMaks = adc;
  if (adc < nilaiMin) nilaiMin = adc;

  // penurunan bertahap agar ambang menyesuaikan perubahan amplitudo
  if (kini - waktuResetAmbang > 10000) {
    nilaiMaks = nilaiMin + (nilaiMaks - nilaiMin) / 2;
    waktuResetAmbang = kini;
  }
  int ambang = (nilaiMaks + nilaiMin) / 2;

  // deteksi puncak: naik melewati ambang, jeda minimal 250 ms (refraktori)
  if (!diAtasAmbang && adc > ambang + 15 && kini - waktuPuncakTerakhir > 250) {
    diAtasAmbang = true;
    if (waktuPuncakTerakhir > 0) {
      unsigned long ibi = kini - waktuPuncakTerakhir;   // jarak antar detak
      if (ibi > 300 && ibi < 2000) {                    // rentang wajar 30-200 bpm
        if (ibiJumlah < 5) ibiRiwayat[ibiJumlah++] = ibi;
        else {
          for (byte i = 0; i < 4; i++) ibiRiwayat[i] = ibiRiwayat[i + 1];
          ibiRiwayat[4] = ibi;
        }
        unsigned long total = 0;
        for (byte i = 0; i < ibiJumlah; i++) total += ibiRiwayat[i];
        bpm = 60000UL / (total / ibiJumlah);
      }
    }
    waktuPuncakTerakhir = kini;
    digitalWrite(PIN_LED, HIGH);
  } else if (diAtasAmbang && adc < ambang - 15) {
    diAtasAmbang = false;
    digitalWrite(PIN_LED, LOW);
  }

  // laporan ke serial monitor tiap 1 detik
  if (kini - waktuSerial >= 1000) {
    waktuSerial = kini;
    Serial.print("BPM: ");
    if (bpm > 0) {
      Serial.print(bpm);
      if (bpm < BPM_MIN || bpm > BPM_MAX) Serial.print("  ! DI LUAR NORMAL (60-100)");
    } else {
      Serial.print("--");
    }
    Serial.print("  | ADC maks-min: ");
    Serial.print(nilaiMaks);
    Serial.print("-");
    Serial.println(nilaiMin);
  }

  delay(2);
}
