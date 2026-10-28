### Jak zbudować APK z własnych płyt

English: [README](../README.md#how-to-build-the-apk-from-your-own-discs).

![Kompilator Tekken 3](builder.png)

Potrzebujesz komputera z Windows, własnej płyty Tekken 3 i telefonu z Androidem 8.0 lub nowszym
(64-bitowym). Kompilator nie zawiera danych gry i niczego nie pobiera: APK powstaje z Twoich
zrzutów, na Twoim komputerze.

**1. Zrób zrzut płyty.** Płyta japońska jest wymagana; płyta USA jest opcjonalna i dodaje
angielskie teksty oraz listy ruchów.

| Płyta | Nazwa Redump | Numer | |
|---|---|---|---|
| japońska | Tekken 3 (Japan) (Rev 1) | SLPS-01300 | zalecana |
| japońska | Tekken 3 (Japan) | SLPS-01300 | pierwsze wydanie, też działa |
| USA | Tekken 3 (USA) | SLUS-00402 | opcjonalna |

Innych wydań (na przykład europejskiej płyty PAL) kompilator nie czyta.

- Zrzuć płytę jako **BIN/CUE** (surowe sektory 2352 bajty) dowolnym napędem CD i programem do
  zrzutów, na przykład ImgBurn (tryb „Create image file from disc”) albo redumper. Plik `.cue`
  i jego plik(i) `.bin` trzymaj razem, w osobnym folderze dla każdej płyty.
- **Nie** zrzucaj do `.iso`: ISO zachowuje tylko zwykłe sektory danych, więc muzyka i filmy
  (sektory XA) przepadają.
- Obrazu `.chd` kompilator nie czyta wprost: najpierw rozpakuj go programem chdman z pakietu MAME:
  `chdman extractcd -i "Tekken 3.chd" -o "Tekken 3.cue"`. Obrazy skompresowane ECM (`.bin.ecm`)
  są czytane bez rozpakowywania.

**2. Opcjonalnie: zestaw automatu.** Zestaw MAME `tekken3` (Tekken 3, World ver. E1, 1997) jako
`tekken3.zip`, zrzucony z własnej płyty automatu, dodaje sceny i modele postaci z automatu
(VERSION: ARCADE w opcjach gry). Starsze zestawy MAME z układami programu nazwanymi
`tet1vere.2e` / `tet1vere.2j` też przechodzą.

**3. Zbuduj.** Pobierz `Tekken3Builder.exe` z
[najnowszego wydania](https://github.com/gangg111/t3native/releases/latest) i uruchom go (jeden
plik, niczego nie trzeba instalować). Wskaż folder płyty japońskiej, opcjonalnie folder płyty USA
i `tekken3.zip`, oraz folder na gotowy plik. Okno sprawdza każdy wybór i podaje rozpoznane
wydanie albo mówi, co jest nie tak. Kliknij ZBUDUJ APK: pierwsza budowa trwa kilka minut i zapisuje
`Tekken3.apk` (około 500 MB). EN / PL w rogu okna przełącza język.

**4. Zainstaluj.** Skopiuj `Tekken3.apk` na telefon i otwórz go; Android zapyta o zgodę na
instalowanie aplikacji z tego źródła (menedżer plików, przeglądarka): zezwól raz. Działa pad
Bluetooth albo USB, a także sterowanie dotykowe na ekranie.

**Aktualizacje i klucz podpisu.** Pierwsza budowa tworzy klucz podpisu w folderze `key` obok
pliku .exe. Zachowaj go: każda następna budowa jest podpisana tym samym kluczem, więc nowy APK
instaluje się na starym i zapisy zostają. Bez niego telefon odrzuci aktualizację i grę trzeba
odinstalować (zapisy przepadną).

**Zachowaj APK dla siebie.** Zawiera dane z Twojej płyty: jest tylko do Twojego prywatnego użytku.

| Problem | Co zrobić |
|---|---|
| „to nie jest obsługiwana płyta Tekken 3” | inne wydanie albo uszkodzony zrzut: zrzuć ponownie, sprawdź tabelę wyżej |
| „to wydanie USA: wskaż je w polu USA” | płyta USA trafiła do pola płyty japońskiej |
| w grze nie ma muzyki ani filmów | płyta była zrzucona do `.iso`: zrzuć ją ponownie jako BIN/CUE |
| „Obrazy .chd nie są obsługiwane” | rozpakuj przez `chdman extractcd` (krok 1) |
| telefon odrzuca aktualizację | APK zbudowany innym kluczem: użyj tego samego folderu `key` albo najpierw odinstaluj grę |
| budowa się nie udała | powód jest na końcu logu w oknie kompilatora |
