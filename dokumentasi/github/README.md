# Dokumentasi GitHub

Bukti pipeline build, rilis, dan kesiapan OTA untuk firmware ESP32.
Semua halaman diambil dari repo publik **akmalseptain/-detak-jantung-esp32**
pada 21 Oktober 2026, tanpa login (tampilan GitHub dark mode).

| File | Halaman | Isi yang ditunjukkan |
|---|---|---|
| `repo.png` | Repository | Struktur repo (`firmware/`, `proteus/`, `dokumentasi/`), commit terakhir `27f138a`, badge **Build & Release Firmware: passing**, rilis v1.2 |
| `actions.png` | Actions | 8 workflow run; run commit `27f138a` (tag `v1.2` dan `main`) berstatus hijau |
| `release.png` | Releases v1.2 | Rilis **v1.2 (Latest)** dari tag `v1.2`, aset `firmware.bin` 1,02 MB |
| `run.png` | Actions run #8 | Status **Success**, durasi **2m19s**, 1 artifact (`firmware` 703 KB) |

Tautan halaman:

- Repository: <https://github.com/akmalseptain/-detak-jantung-esp32>
- Actions: <https://github.com/akmalseptain/-detak-jantung-esp32/actions>
- Release v1.2: <https://github.com/akmalseptain/-detak-jantung-esp32/releases/tag/v1.2>

## Detail teknis yang bisa disebut saat presentasi

- Workflow: `.github/workflows/build.yml` (`Build & Release Firmware`).
- Trigger: push ke `main` (build + artifact) dan tag `v*` (release + aset `firmware.bin`).
- Build memakai `arduino-cli` dengan core `esp32:esp32` dan library Adafruit SSD1306/GFX.
- Rilis v1.2 memperbaiki parsing `tag_name` OTA (v1.1 tidak pernah mendeteksi versi baru).
- `firmware.bin` diunduh perangkat dari `releases/latest`, ditulis lewat `Update.h`,
  lalu perangkat reboot. Jika gagal, firmware lama dipertahankan (rollback).

## Catatan

- Angka durasi/commit dapat berubah setelah push berikutnya; screenshot adalah
  kondisi saat dokumentasi dibuat.
- Screenshot dibuat dengan Chrome headless (`--headless=new`) agar reproducible.
