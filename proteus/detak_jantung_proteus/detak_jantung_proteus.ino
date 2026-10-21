/*
 * =====================================================================
 *  TUGAS AKHIR - SIMULASI PROTEUS (Arduino Uno / ATmega328P)
 *  Alat Pemantau Detak Jantung Low-Cost
 *
 *  Model pengukuran mengikuti repo Heart_Beat_Simulation_Proteus:
 *  sensor detak digital + LCD 20x4 + tombol mulai + hitung 6 detik.
 * =====================================================================
 *  Rangkaian di Proteus:
 *    1. Arduino Uno (ATmega328P)
 *    2. Heart Beat Sensor (library HeartBeatSensorTEP)
 *       OUT sensor  -> D4
 *       Potensiometer -> input sensor (mengatur laju detak)
 *    3. Push button -> D2 (INPUT_PULLUP, aktif LOW)
 *    4. LCD 20x4 (LM044L):
 *       RS=13, E=12, D4=11, D5=10, D6=9, D7=8
 *    5. VCC/GND sensor, potensiometer, dan LCD ke 5V/GND
 *
 *  Alur:
 *    - Tekan tombol untuk mulai mengukur.
 *    - Selama 6 detik Arduino menghitung pulsa HIGH dari sensor.
 *    - BPM = jumlah detak x 10 (60 detik / 6 detik).
 *    - Hasil dan status normal (60-100 BPM) tampil di LCD.
 *
 *  Cara pakai:
 *    - Install library TimerOne (libraries/TimerOne-1.1.0.zip).
 *    - Compile sketch ini di Arduino IDE, ambil file .hex
 *      (Sketch -> Export Compiled Binary).
 *    - Klik dua kali komponen Uno di Proteus -> Program File: pilih .hex.
 *    - Jalankan simulasi.
 * =====================================================================
 */

#include <TimerOne.h>
#include <LiquidCrystal.h>

// ------------------------- PIN ---------------------------------------
const uint8_t PIN_SENSOR = 4;   // OUT heart beat sensor
const uint8_t PIN_TOMBOL = 2;   // push button mulai

LiquidCrystal lcd(13, 12, 11, 10, 9, 8);  // RS, E, D4, D5, D6, D7

// ------------------------- PARAMETER ---------------------------------
const uint8_t DURASI_DETIK = 6;  // jendela pengukuran (detik)
const int BPM_MIN = 60;          // batas normal bawah
const int BPM_MAX = 100;         // batas normal atas

// ------------------------- STATE -------------------------------------
volatile bool tickDetik = false; // diset ISR tiap 1 detik

bool mengukur = false;
uint8_t detikUkur = 0;
uint16_t jumlahDetak = 0;
int bpm = 0;
bool pulsaSebelumnya = LOW;

bool tombolStabil = HIGH;
bool tombolTerakhir = HIGH;
unsigned long waktuTombol = 0;

// ------------------------- PROTOTIPE ---------------------------------
void timerIsr();
void tampilkanIdle();
void tampilkanProgres();
void tampilkanHasil();
void mulaiMengukur();
void selesaiMengukur();
void bacaTombol();
void bacaDetak();

// =====================================================================
//  Timer1: penanda 1 detik. LCD TIDAK ditulis di dalam ISR.
// =====================================================================
void timerIsr() {
  tickDetik = true;
}

// =====================================================================
//  SETUP
// =====================================================================
void setup() {
  pinMode(PIN_SENSOR, INPUT);
  pinMode(PIN_TOMBOL, INPUT_PULLUP);

  Serial.begin(9600);
  lcd.begin(20, 4);
  lcd.clear();

  tampilkanIdle();

  Timer1.initialize(1000000);   // 1 detik
  Timer1.attachInterrupt(timerIsr);

  Serial.println("=== Simulasi Detak Jantung - Proteus ===");
  Serial.println("Tekan tombol untuk mulai mengukur (6 detik).");
}

// =====================================================================
//  TAMPILAN
// =====================================================================
void tampilkanIdle() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(" Heart-Beat Sensor  ");
  lcd.setCursor(0, 1);
  lcd.print(" Alat Detak Jantung ");
  lcd.setCursor(0, 2);
  lcd.print("      Low-Cost      ");
  lcd.setCursor(0, 3);
  lcd.print("  Tekan Tombol...   ");
}

void tampilkanProgres() {
  lcd.setCursor(0, 1);
  lcd.print(" Menghitung...      ");
  lcd.setCursor(0, 2);
  lcd.print(" Detak: ");
  lcd.print(jumlahDetak);
  lcd.print("          ");
  lcd.setCursor(0, 3);
  lcd.print(" Sisa: ");
  lcd.print(DURASI_DETIK - detikUkur);
  lcd.print(" dtk     ");
}

void tampilkanHasil() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(" Heart-Beat Sensor  ");
  lcd.setCursor(0, 1);
  lcd.print(" HB Per Min: ");
  if (bpm > 0) {
    lcd.print(bpm);
  } else {
    lcd.print("--");
  }
  lcd.print("    ");

  lcd.setCursor(0, 2);
  if (bpm > 0 && (bpm < BPM_MIN || bpm > BPM_MAX)) {
    lcd.print(" ! DI LUAR NORMAL ! ");
  } else if (bpm > 0) {
    lcd.print("   Status: Normal   ");
  } else {
    lcd.print("  Tidak ada pulsa   ");
  }

  lcd.setCursor(0, 3);
  lcd.print(" Tekan Tombol Lagi  ");
}

// =====================================================================
//  PENGUKURAN
// =====================================================================
void mulaiMengukur() {
  mengukur = true;
  detikUkur = 0;
  jumlahDetak = 0;
  bpm = 0;
  tickDetik = false;
  pulsaSebelumnya = digitalRead(PIN_SENSOR);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(" Heart-Beat Sensor  ");
  tampilkanProgres();

  Serial.println("[MULAI] Menghitung detak selama 6 detik...");
}

void selesaiMengukur() {
  mengukur = false;
  bpm = jumlahDetak * (60 / DURASI_DETIK);   // 6 detik -> x10

  tampilkanHasil();

  Serial.print("HB Per Min: ");
  Serial.print(bpm);
  if (bpm > 0 && (bpm < BPM_MIN || bpm > BPM_MAX)) {
    Serial.print("  ! DI LUAR NORMAL (60-100)");
  }
  Serial.println();
}

void bacaTombol() {
  bool baca = digitalRead(PIN_TOMBOL);

  if (baca != tombolTerakhir) {
    tombolTerakhir = baca;
    waktuTombol = millis();
  }

  if (millis() - waktuTombol > 50 && baca != tombolStabil) {
    tombolStabil = baca;
    if (tombolStabil == LOW) {
      mulaiMengukur();   // bisa dipakai untuk mengulang pengukuran
    }
  }
}

void bacaDetak() {
  bool pulsa = digitalRead(PIN_SENSOR);

  // hitung tepi naik (satu pulsa = satu detak)
  if (pulsa == HIGH && pulsaSebelumnya == LOW) {
    jumlahDetak++;
    tampilkanProgres();
  }

  pulsaSebelumnya = pulsa;
}

// =====================================================================
//  LOOP UTAMA
// =====================================================================
void loop() {
  bacaTombol();

  if (mengukur) {
    bacaDetak();

    if (tickDetik) {
      tickDetik = false;
      detikUkur++;

      if (detikUkur >= DURASI_DETIK) {
        selesaiMengukur();
      } else {
        tampilkanProgres();
      }
    }
  }

  delay(2);
}
