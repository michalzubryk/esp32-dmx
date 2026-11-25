# HTTP API i Art-Net

[Start](../../README.md) | [English](../en/api.md)

Domyslna baza AP: `http://192.168.44.1`. Komendy zmieniaja rzeczywiste wyjscie. HTTP 200 potwierdza odbior przez modul, nie przez lampe.

## Endpointy

| Metoda | Sciezka | Dane / wynik |
| --- | --- | --- |
| GET | `/api/status` | JSON: wersja, sprzet, adresy, linki, universe, ramki, pakiety, heap, demo |
| GET | `/api/logs` | Tekstowy bufor do 60 logow |
| GET | `/api/config` | Konfiguracja, takze dane dostepowe sieci; tylko zaufani klienci |
| POST | `/api/config` | Pelna konfiguracja formularza, zapis w NVS i restart |
| GET | `/api/profiles` | Obiekt zapisanych profili; wbudowane sa osobno, chyba ze zapisano je importem |
| POST | `/api/profiles` | JSON zastepujacy zapisany obiekt; 2–16 000 bajtow; 200, 400 lub 507 |
| POST | `/api/range` | `addr` i `values` rozdzielone przecinkami |
| POST | `/api/dmx` | Opcjonalne `c1` do `c8`, stare sterowanie pierwszymi kanalami |
| POST | `/api/fixture` | `dim,r,g,b,w,mode,strobe`; SHE 7CH od adresu 1 |
| POST | `/api/demo` | `name`: demo firmware |
| POST | `/api/blackout` | Zatrzymuje demo firmware i zeruje wszystkie kanaly |
| GET | `/api/wifi/scan` | Tekstowy wynik skanowania |
| POST | `/api/update` | Multipart z firmware, OTA i restart |

Komendy POST poza profilami i OTA przyjmuja `application/x-www-form-urlencoded`. Nieznana sciezka zwraca panel WWW: sam status 200 nie potwierdza istnienia endpointu.

## Zakres Kanalow

```sh
curl --fail http://192.168.44.1/api/status
curl --fail http://192.168.44.1/api/range \
  --data 'addr=1&values=255,255,0,0,0,0,0'
curl --fail -X POST http://192.168.44.1/api/blackout
```

Wartosci sa ograniczane do 0–255, adres do 1–512. Nadmiar poza 512 jest obcinany. Pusta lista daje 400. Parser nie jest scislym walidatorem liczb: klient powinien sprawdzac wejscie. Range i fixture wylaczaja demo firmware, ale inne zrodlo Art-Net moze nadal zmieniac bufor.

## Profile

```sh
curl --fail http://192.168.44.1/api/profiles
curl --fail http://192.168.44.1/api/profiles \
  -H 'Content-Type: application/json' \
  --data-binary @examples/laser10-profile.json
```

API zastepuje caly zapisany obiekt; importer WWW najpierw laczy profile. Serwer sprawdza glownie rozmiar i poczatkowy `{`. Pelna walidacja profili dziala w UI, dlatego klient API musi sam sprawdzac JSON. [Format profili](guide.md#profile-urzadzen-i-sceny).

## Konfiguracja Sieci

Wysylaj wszystkie pola razem: `hostname,uniqueNames,apEnabled,apSsid,apPass,apIp,staEnabled,staSsid,staPass,ethEnabled,ethDhcp,ethIp,ethGateway,ethSubnet,ethDns,universe`. Booleany to `1` i `0`. Brakujace pola nie oznaczaja czesciowej aktualizacji. Najlepiej uzyj formularza WWW. Nie publikuj pobranej konfiguracji i hasel.

## Demo Firmware

Nazwy: `red`, `green`, `blue`, `white`, `all-on`, `shehds-color`, `fast-chase`, `fade-rgbw`, `dimmer-ramp`, `shehds-strobe`, `channel-scan`, `mode-scan`. Inna nazwa wybiera `off`. Te demo maja zalozenia RGBW/7CH; nie sa uniwersalne dla importowanych profili. Sceny profili i programator uzywaja `/api/range`.

## OTA

```sh
curl --fail http://192.168.44.1/api/update \
  -F 'update=@.pio/build/esp32-c3-devkitm-1/firmware.bin'
```

Dobierz obraz aplikacji do sprzetu. [Instrukcja budowania](development.md). Endpoint nie ma uwierzytelniania.

## Art-Net

- UDP 6454, identyfikator `Art-Net\0`.
- ArtDMX: opcode `0x5000` little-endian, wersja protokolu 14 big-endian.
- Port-Address/universe little-endian, domyslnie 0; niektore programy pokazuja to jako universe 1.
- Dlugosc danych big-endian, parzysta liczba kanalow do 512.
- Obslugiwane ArtPoll. Brak RDM, odczytu patcha i wejscia DMX.
- Brak scalania i priorytetow kontrolerow. Wyjscie utrzymuje bufor.

[Przyklad Python](../../examples/send_artnet.py) domyslnie wysyla same zera. Wartosci testowe trzeba podac jawnie.

```sh
python3 examples/send_artnet.py --host 192.168.44.1 --seconds 2
python3 examples/send_artnet.py --host 192.168.44.1 --address 1 \
  --values 128 --seconds 3 --blackout
```
