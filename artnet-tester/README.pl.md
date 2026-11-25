# Tester Art-Net DMX

Polski | [English](README.md)

Opcjonalna aplikacja WWW w Pythonie, wysylajaca ArtDMX po UDP. Nie uzywa API HTTP ESP32 i nie jest wgrywana do modulu.

```sh
docker compose up --build
# Panel: http://127.0.0.1:8080/
```

Uzywane jest `network_mode: host` na Linuxie. Host potrzebuje trasy do modulu i UDP 6454. Domyslnie: cel `192.168.44.1`, universe 0, WWW 8080.

```sh
ARTNET_TARGET=192.168.1.50 HTTP_PORT=18080 docker compose up --build
```

Panel ma surowe kanaly, presety SHE-FS403A RGBW, dimmer Botex DPX-620 III (adres 450) i testy wielu urzadzen. Porownaj presety z instrukcja lampy. Cztery urzadzenia 7CH domyslnie maja adresy `100,107,114,121`, bez nakladania kanalow. Dopasuj je do patcha.

Zmienne: `ARTNET_TARGET`, `ARTNET_UNIVERSE`, `HTTP_PORT`, `DPX_ADDRESS`, `PIXEZOOM_ADDRESSES`, `PIXEZOOM_CHANNELS`.

Brak uwierzytelniania: uzywaj zaufanej sieci. Tester wysyla pelne 512 kanalow i moze zastapic wartosci innego kontrolera. Presety testera sa niezalezne od katalogu profili w module.
