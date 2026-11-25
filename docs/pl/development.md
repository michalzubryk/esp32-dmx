# Budowanie, Wgrywanie i OTA

[Start](../../README.md) | [English](../en/development.md)

## Przygotowanie

```sh
git clone https://github.com/michalzubryk/esp32-dmx.git
cd esp32-dmx
python3 -m venv .venv
.venv/bin/pip install platformio
.venv/bin/pio run -e esp32-c3-devkitm-1
```

PlatformIO pobiera platforme Espressif32, Arduino i kompilatory. Nie potrzeba prywatnych danych Wi-Fi ani cache autora. Zachowano oryginalne `platform = espressif32` bez przypiecia wersji; dla scislej odtwarzalnosci zaleznosci przypnij wersje w kontrolowanym wdrozeniu.

## Warianty

| Srodowisko | Sprzet / dzialanie |
| --- | --- |
| `esp32-c3-devkitm-1` | C3, TX GPIO4, DE GPIO5 |
| `esp32s` | ESP32 Dev Module, TX GPIO4, DE GPIO5 |
| `esp32-s3` | S3 DevKitC-1, TX GPIO4, DE GPIO5 |
| `esp32-eth01` | LAN8720 PHY 1, TX GPIO4, DE GPIO33 |
| `esp32-eth01-ethonly` | ETH01; wymusza Ethernet-only i DHCP przy kazdym starcie |
| `esp32-eth01-autonet` | ETH01 z automatycznym wylaczaniem i przywracaniem Wi-Fi |
| `esp32-eth01-phy0` | ETH01 z adresem PHY 0 |

To konfiguracje kompilacji, nie deklaracja fizycznego przetestowania kazdej rewizji plytki. Porownaj piny i polaczenia PHY ze swoim sprzetem.

## USB

```sh
.venv/bin/pio run -e esp32s -t upload --upload-port /dev/ttyUSB0
.venv/bin/pio device monitor --baud 115200
```

Wybierz swoje srodowisko. ETH01 potrzebuje adaptera i recznego bootloadera, zgodnie z [instrukcja sprzetu](hardware.md). CI nigdy nie wykonuje uploadu do urzadzenia.

## OTA

Zbuduj wariant pasujacy do obecnego sprzetu. W Konfiguracji wybierz `.pio/build/<srodowisko>/firmware.bin` i uruchom aktualizacje WWW. Nie wgrywaj ELF, bootloadera, tablicy partycji, obrazu laczonego ani obrazu dla innej plytki. Po sukcesie modul sie restartuje; konfiguracja i profile NVS normalnie pozostaja. Przy braku startu lub miejsca partycji OTA odzyskaj modul przez USB-UART.

## Struktura

```text
platformio.ini             Warianty plytek
src/main.cpp              UART DMX, Art-Net, HTTP API, glowny panel WWW
src/profiles_ui.h         Dynamiczne profile, laser10, katalog wzorow
examples/                 Profile JSON i nadajnik ArtDMX
tools/preview.py           Lokalny symulator API do podgladu interfejsu
tools/screenshots.py       Zrzuty przez Chrome DevTools
artnet-tester/            Osobny tester Python/Docker
docs/en/ i docs/pl/        Dokumentacja dwujezyczna
.github/workflows/        Automatyczne kompilacje
```

## Podglad i Zrzuty

```sh
python3 tools/preview.py
# Otworz http://127.0.0.1:8765/
```

Podglad wyciaga HTML/JS z firmware i udostepnia sztuczne API. Nasluchuje tylko lokalnie i nie wysyla DMX. Nie zastepuje firmware ani kontrolera. Do ponownego wykonania zrzutow potrzebny jest Google Chrome i biblioteka `websocket-client` w srodowisku Python; uruchom `tools/screenshots.py`. Zrzuty powinny zawierac wylacznie dane demonstracyjne.

## Sprawdzanie

Zbuduj warianty przez `pio run -e <srodowisko>`, sprawdz skladnie skryptow przez `python3 -m py_compile examples/send_artnet.py tools/preview.py tools/screenshots.py`, poprawny JSON i podglad desktop/mobile. CI buduje wszystkie warianty i zapisuje obrazy aplikacji jako artefakty. Sukces kompilacji nie potwierdza elektrycznego sygnalu DMX ani poprawnosci dzialania lampy.

## Tester Docker

```sh
cd artnet-tester
docker compose up --build
```

Tester jest osobna aplikacja hosta, nie firmware ESP32. Potrzebuje trasy do modulu i dostepu UDP 6454. Przed uzyciem presetow sprawdz README i zgodnosc profilu lampy.
