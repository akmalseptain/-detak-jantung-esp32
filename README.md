# TA Detak Jantung ESP32 — Alat Pemantau Detak Jantung Low-Cost

Sistem pemantau detak jantung murah: **pulse sensor + ESP32 + OLED**, dengan
**simulasi Proteus** (Arduino Uno sebagai model logika) dan **OTA firmware via
GitHub Releases** (ESP32 mengunduh firmware baru sendiri).

## Struktur repo

```
proteus/detak_jantung_proteus/detak_jantung_proteus.ino  -> sketch simulasi Proteus (Arduino Uno)
firmware/detak_jantung/detak_jantung.ino                -> firmware utama (ESP32)
.github/workflows/build.yml                             -> build otomatis + rilis firmware
```

## 1. Simulasi di Proteus

1. Buka Proteus, siapkan komponen: **Arduino Uno (ATmega328P)**, **Signal
   Generator / DC Pulse**, **Virtual Terminal**, **Oscilloscope**.
2. Hubungkan keluaran signal generator ke pin **A0** (pengganti pulse sensor).
3. Set frekuensi sesuai skenario uji: 60 / 80 / 100 / 120 bpm
   (1 / 1.33 / 1.67 / 2 Hz).
4. Compile `proteus/detak_jantung_proteus/detak_jantung_proteus.ino` di
   Arduino IDE, ambil file **.hex**-nya (Sketch -> Export Compiled Binary).
5. Klik dua kali komponen Uno di Proteus -> Program File: pilih .hex -> jalankan.

Yang bisa di-screenshot untuk slide bukti:
- Skematik rangkaian Proteus (Uno + signal generator + terminal)
- Virtual Terminal / serial monitor: `BPM: 72 | ADC maks-min: ...`
- Oscilloscope sinyal di A0

Catatan: Proteus tidak punya model ESP32, jadi logika disimulasikan di
Arduino Uno — algoritma deteksi puncaknya sama dengan firmware ESP32.

## 2. Build & rilis otomatis (GitHub Actions)

- Setiap push ke `main`: firmware ESP32 dikompilasi otomatis (arduino-cli).
- Setiap tag baru `v*` (contoh: `git tag v1.1 && git push --tags`):
  GitHub Actions membuat **Release** berisi `detak_jantung.ino.bin`.

## 3. Cara kerja OTA di perangkat

1. Firmware memanggil GitHub API `releases/latest` (sekali per 6 jam).
2. Bila tag versi lebih baru dari `FIRMWARE_VERSION`, file firmware `.bin`
   diunduh via HTTPS.
3. `Update.h` menulis firmware baru ke partisi OTA; jika gagal, firmware
   lama dipertahankan (rollback otomatis ESP32).
4. Perangkat reboot menjalankan versi baru — tanpa kabel USB.

## 4. Wiring hardware asli

| Komponen             | Pin ESP32 |
|----------------------|-----------|
| Pulse sensor (SIG)   | GPIO 34   |
| Pulse sensor (VCC)   | 3V3       |
| Pulse sensor (GND)   | GND       |
| OLED SDA             | GPIO 21   |
| OLED SCL             | GPIO 22   |
| LED indikator        | GPIO 2    |

## 5. Catatan

- `klien.setInsecure()` di kode OTA sementara; sebelum sidang ganti dengan
  sertifikat GitHub yang disematkan (lebih aman).
- Isi `WIFI_SSID`, `WIFI_PASS`, dan `OTA_REPO` sebelum flash ke hardware.
- Logika pembacaan BPM sudah sama antara simulasi dan hardware — hanya
  platform yang berbeda (Uno di Proteus, ESP32 di alat asli).
