# Urzadzenia: Zakres Prob i Zrodla

[Start](../../README.md) | [English](../en/devices.md)

| Urzadzenie | Zakres potwierdzenia | Gotowe sterowanie w tym repozytorium |
| --- | --- | --- |
| SHEHDS / SHE-FS403A, LED PAR 54x3W RGBW 7CH | Testy opisane w oryginalnym projekcie firmware | Profil `she-fs403a-7ch`: dimmer, RGBW, program i strobe/speed |
| Projektor lasera 10CH, model nieustalony | Uzytkownik potwierdzil wzory, dwa kolka przy CH2=150, zmiany wielkosci i rysowania | Profil `laser10`, surowe kanaly, sceny i katalog opisow |
| Robe Pointe Mode 1 | Potwierdzona reakcja na `1 At 50` wyslane do grandMA3 po OSC; pozniejsze proby kolorow/gobo nie stanowia pelnej walidacji | Brak dedykowanego profilu Pointe; potrzebna poprawna definicja wedlug tabeli DMX |
| Sciemniacz 6CH / Botex DPX-620 III | Jest przyklad profilu i sekcja testera; brak potwierdzenia fizycznego testu w tym pakiecie | Generic RAW 6CH i osobny tester Art-Net |

Nie wszystkie urzadzenia byly sterowane ta sama droga. Sterowanie Pointe przez konsole grandMA3 nie potwierdza bezposredniego profilu Pointe na ESP32. Laser byl sterowany przez modul HTTP i Art-Net. Wyslanie pakietu lub HTTP 200 potwierdza jedynie odbior przez modul; reakcje lampy trzeba obserwowac.

## Zdjecia i Linki

- SHEHDS 54x3W RGBW: [oficjalna karta produktu](https://shehds.com/products/fast-shipping-led-54x3w-rgbw-led-flat-par-rgbw-color-mixing-dj-wash-light-stage-uplighting-ktv-disco-dj-dmx512), [instrukcje producenta](https://shehds.com/pages/user-manual-download), [oferta bedaca zrodlem zdjecia katalogowego](https://www.ebay.com/itm/286060053321). Nazwa `SHE-FS403A` pochodzi z lokalnej dokumentacji; nie zakladamy, ze kazda lampa 54x3W ma ten sam profil.
- Robe Pointe: [oficjalna karta, zdjecia i dokumentacja](https://www.robe.cz/pointe). Zdjecie jest osadzone bezposrednio z CDN Robe.
- Laser: nieustalony model, wiec brak zdjecia i oferty przypisanej jako zgodny produkt. Podobna tabela innego producenta nie wystarcza do identyfikacji.

Zdjecia sluza identyfikacji rodziny produktu, nie dokumentuja naszych fizycznych testow. Sa zewnetrznymi zasobami i nie sa objete licencja MIT repozytorium. Nie sugerujemy partnerstwa ani certyfikacji producentow.
