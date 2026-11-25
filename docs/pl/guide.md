# Instrukcja: Profile, Sceny i Obsluga

[Start](../../README.pl.md) | [English](../en/guide.md)

## Uruchomienie i Siec

Nowy modul tworzy `DMX-Node-C3-XXXX`, haslo `dmx12345`, adres AP `192.168.44.1/24`. Sufiks pochodzi z MAC i mozna go wylaczyc. Nazwa hosta domyslnie ma prefix `esp32-c3-dmx`, takze na innych wariantach plytek. Publiczny kod nie zawiera danych domowej sieci Wi-Fi.

W Konfiguracji zmien adres i haslo AP, wlacz STA z danymi swojej sieci, ustaw universe lub Ethernet. Zapis konfiguracji sieciowej restartuje modul. Proba STA dziala w tle; przy wlaczonym AP nieudane polaczenie przechodzi po okolo 20 sekundach w AP-only. Ethernet wymaga wariantu ETH01. `autonet` wylacza Wi-Fi po zestawieniu Ethernetu i przywraca je po utracie linku. Wariant ETH-only wymaga poprawnej sieci przewodowej.

Konfiguracja z NVS ma pierwszenstwo przed wartosciami w kodzie. OTA nie przywraca ustawien fabrycznych.

## Widoki WWW

| Widok | Zastosowanie |
| --- | --- |
| Start | Adresy, porty, uslugi i informacje |
| Live | Profil, adres, kanaly, kolo kolorow lub surowe suwaki, sceny, blackout |
| Programy | Nagrywanie, edycja i usuwanie krokow, czas, petle, zapis/odczyt |
| Monitor | Liczniki ramek i pakietow, pamiec, logi |
| Konfiguracja | Siec, import/eksport profili i OTA |

W profilach surowych zmiana formularza wymaga klikniecia Ustaw. Przyciski nazwanych scen wysylaja wartosci od razu. Wizualizacja jest pogladowa: nie potwierdza rzeczywistego swiecenia ani geometrii lasera.

## Profile Urzadzen i Sceny

Wbudowane profile: SHE-FS403A 7CH, RGBW 4CH, RAW 8CH i laser 10CH. Profil opisuje liczbe i kolejnosc kanalow, etykiety, zakresy, ustawienia poczatkowe, nazwane sceny i blackout urzadzenia. Profile bezposrednie moga miec do 512 kanalow, z ograniczeniem adresu startowego i 16 000 bajtow calego zapisanego JSON.

Konfiguracja → Import profili JSON laczy poprawne profile z aktualnymi i zapisuje je w NVS. Eksport pobiera JSON. Profile pozostaja po restarcie i sa dostepne na innym kliencie. Import tego samego identyfikatora zastepuje jego definicje. Nie ma osobnego przycisku usuwania profilu; usuniecie wpisu przez API nie usuwa profilu wbudowanego w firmware.

Przyklady: [`laser10-profile.json`](../../examples/laser10-profile.json) i [`generic-profile-example.json`](../../examples/generic-profile-example.json). Rzeczywisty importer oczekuje obiektu z kluczami identyfikatorow, a nie dawnej propozycji `schema/model/channelCount`.

```json
{
  "my-dimmer": {
    "type": "raw",
    "label": "Dimmer 1CH",
    "channels": 1,
    "labels": ["Dimmer"],
    "defaults": [0],
    "ranges": [[{"label": "Pelny zakres", "min": 0, "max": 255}]],
    "scenes": {"Polowa": [128], "Pelna moc": [255]},
    "blackout": {"1": 0}
  }
}
```

`defaults` i kazda scena musza zawierac dokladnie `channels` liczb calkowitych 0–255. Identyfikator moze zawierac litery, cyfry, `_` i `-`. Numery w `blackout` sa liczone od 1 wzgledem adresu urzadzenia. Typ `raw` wysyla kanaly kolejno, bez mapowania RGBW.

## Katalog Wzorow

W laser10 kanal CH2 wybiera wzor w trybie recznym. Wybierz surowa wartosc, wpisz opis i zapisz. Obiekt `patterns` mapuje wartosci na nazwy, np. `{"150":"Dwa kolka"}`. `patternChannel: 2` uruchamia katalog dla innego profilu bezposredniego.

Katalog pokazuje 256 surowych wartosci. Instrukcja podaje 51 wzorow, ale ich dokladne granice nie zostaly ustalone. Dwa kolka przy CH2=150 zaobserwowano na testowanym projektorze; nie jest to uniwersalna mapa producenta.

## Programator i Zarzadzanie Scenami

1. Wybierz profil i adres, ustaw kanaly.
2. W Programach wybierz czas kroku i dodaj aktualne wartosci.
3. Kliknij krok, zmien kanaly, czas lub adres i podmien zaznaczony krok.
4. Usun zbedne kroki albo wyczysc program. Zapisuj i wczytuj programy pod nazwa.
5. Ustaw powtorzenia: 0 to nieskonczona petla, 1 pojedyncze odtworzenie, wieksza liczba to liczba petli.
6. Play wysyla kolejne kroki po HTTP; Stop zatrzymuje harmonogram przegladarki.

Krok przechowuje ID profilu, adres, `ms` oraz kompletna tablice kanalow `values`, takze powyzej CH8. Programy wykonuje przegladarka, nie ESP32: zamkniecie lub uspienie strony przerywa odtwarzanie. Nazwane programy sa w localStorage konkretnej przegladarki. Sceny zapisane wewnatrz profilu mozna eksportowac i sa trwale w module po imporcie.

Stop nie oznacza blackoutu. Blackout urzadzenia dotyczy wybranego profilu, a globalny Blackout/API zeruje wszystkie 512 kanalow. Kolejny pakiet z innego kontrolera moze ponownie ustawic wartosci.

## Laser 10CH

| Kanal | Funkcja | Zakres |
| --- | --- | --- |
| 1 | Tryb | 0–63 off; 64–127 reczny; 128–191 auto; 192–255 sound |
| 2 | Wzor/program | Reczny: 51 wzorow w 0–255; auto/sound: cztery zakresy po 64 wartosci |
| 3 | Obrot | 0–127 kat; 128–191 predkosc w przod; 192–255 wstecz |
| 4–5 | Przechylenie poziome/pionowe | 0–127 pozycja; 128–255 ruch automatyczny |
| 6–7 | Przesuniecie poziome/pionowe | 0–127 pozycja; 128–255 ruch automatyczny |
| 8 | Rozmiar | 0–63 statyczny duzy → maly; pozostale zakresy animuja skalowanie |
| 9 | Kolor | 0–63 pojedynczy; 64–127 mieszany; 128–191 pojedynczy auto; 192–255 mieszany auto |
| 10 | Rysowanie | Surowe 0–255; obserwowano linie, punkty, kreski i rysowanie po obrysie; granice nieustalone |

Przyklad: `100,150,64,64,64,64,64,0,0,0`. Blackout ustawia tylko kanal trybu na 0. Porownaj profil z instrukcja konkretnej lampy. Ten profil nie ma osobnego potwierdzonego dimmera.

## Diagnostyka

- Brak WWW: sprawdz SSID, brame, zapisany adres AP, DHCP i tryb ETH-only. Starsze wersje uzywaly `192.168.4.1`.
- WWW dziala, lampa nie: sprawdz tryb DMX, adres, liczbe kanalow, kabel, mase, zasilanie i DE. ETH01 wymaga GPIO33 zamiast GPIO5.
- Losowe blyski: sprawdz polaryzacje, wspolna mase, terminator i sygnal RS485. Liczniki nie potwierdzaja fizycznej poprawnosci DMX ani efektu optycznego.
- Art-Net nie dziala: sprawdz UDP 6454, firewall, routing i universe od zera. To nie jest OSC.
- Samoczynne zmiany: zatrzymaj inne przegladarki, demo lub kontrolery. Brak scalania HTP/LTP i priorytetow.
- Blad importu: wymagany obiekt profili, poprawne tablice i laczny limit 16 000 bajtow.
- Blad OTA: wybierz firmware dla poprawnej plytki i sprawdz miejsce partycji OTA; awaryjnie uzyj USB-UART.
