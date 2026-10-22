Tekken 3 Builder (native Android port)

The builder makes the game's APK from your own Tekken 3 disc. It holds no game data and
downloads nothing; everything made from the disc stays on your computer. Nothing needs to be
installed: the builder is one file that carries everything it needs.

How to use it
1. Run "Tekken 3 Builder.exe". The first start prepares the builder (a few seconds).
2. Choose the folder of the Japanese PlayStation disc image: a .cue file with its .bin tracks.
   CHD images are not read: extract them to .cue/.bin first (chdman extractcd from MAME).
3. Optionally: the folder of the USA disc image (English texts and move lists) and MAME's arcade
   set tekken3.zip (the arcade's stages and character models, the VERSION: ARCADE option).
   The window checks each choice and names the release it found, or says what is wrong.

Supported releases (Redump names)
- Japanese disc (required), either of:
    Tekken 3 (Japan) (Rev 1)   SLPS-01300   recommended
    Tekken 3 (Japan)           SLPS-01300   the first release
- USA disc (optional):
    Tekken 3 (USA)             SLUS-00402
- Arcade (optional): MAME's set "tekken3" (Tekken 3, World ver. E1, 1997), as tekken3.zip.
  Older MAME sets that name its program chips tet1vere.2e / tet1vere.2j are accepted too.
Other releases (for example the European PAL disc) are not read: the window says so.
4. Click BUILD APK. The first build takes a few minutes. Tekken3.apk is written to the chosen
   folder. EN / PL in the window's corner switches the language.

Signing key
The first build makes a unique signing key in the "key" folder next to the builder. Every later
build signs the APK with the same key, so a new version installs on the phone as an update and
the saves are kept. Do not delete the "key" folder; when moving the builder, move it along.
Without it the phone refuses the update and the game has to be uninstalled (the saves are lost).

The APK holds data from your disc: for your own private use only, do not share it.

Licence: the port's code comes from the Tekken3Remake project (PolyForm Noncommercial 1.0.0,
LICENSE.md), for noncommercial use only. The FFmpeg inside is FFmpeg 8.0.1 under the LGPL 2.1
(built without its GPL parts; sources: ffmpeg.org; the libraries' licences in
runtime\ffmpeg\licenses). Tekken is a trademark of Bandai Namco Entertainment; this project is
not affiliated with it.


Kompilator Tekken 3 (natywny port na Androida)

Kompilator buduje plik APK gry z Twojej własnej płyty Tekken 3. Nie zawiera żadnych danych gry
i niczego nie pobiera z internetu. Wszystko, co powstaje z płyty, zostaje na Twoim komputerze.
Niczego nie trzeba instalować: kompilator to jeden plik, który ma w sobie wszystko.

Jak używać
1. Uruchom "Tekken 3 Builder.exe". Pierwszy start przygotowuje kompilator (kilka sekund).
2. Wskaż folder z obrazem japońskiej płyty PlayStation: plik .cue ze ścieżkami .bin. Obrazów
   .chd kompilator nie czyta: rozpakuj je najpierw do .cue/.bin (chdman extractcd z MAME).
3. Opcjonalnie: folder z obrazem płyty USA (angielskie teksty i listy ruchów) oraz zestaw MAME
   automatu tekken3.zip (sceny i modele postaci z automatu, opcja VERSION: ARCADE).
   Okno sprawdza każdy wybór i podaje rozpoznane wydanie albo mówi, co jest nie tak.

Obsługiwane wydania (nazwy Redump)
- Płyta japońska (wymagana), jedna z:
    Tekken 3 (Japan) (Rev 1)   SLPS-01300   zalecana
    Tekken 3 (Japan)           SLPS-01300   pierwsze wydanie
- Płyta USA (opcjonalna):
    Tekken 3 (USA)             SLUS-00402
- Automat (opcjonalnie): zestaw MAME "tekken3" (Tekken 3, World ver. E1, 1997) jako tekken3.zip.
  Starsze zestawy MAME z układami programu nazwanymi tet1vere.2e / tet1vere.2j też przechodzą.
Innych wydań (na przykład europejskiej płyty PAL) kompilator nie czyta: okno to powie.
4. Kliknij BUILD APK. Pierwsza budowa trwa kilka minut. Gotowy Tekken3.apk trafia do
   wybranego folderu. EN / PL w rogu okna przełącza język.

Klucz podpisu
Przy pierwszej budowie kompilator tworzy własny, unikalny klucz podpisu w folderze "key" obok
kompilatora. Każda następna budowa podpisuje APK tym samym kluczem, więc nowa wersja instaluje
się na telefonie jako aktualizacja, bez utraty zapisów. Nie usuwaj folderu "key"; przenosząc
kompilator, przenieś go razem z nim. Bez niego telefon odrzuci aktualizację i grę trzeba
odinstalować (zapisy przepadną).

Gotowy plik APK zawiera dane z Twojej płyty: jest tylko do Twojego prywatnego użytku,
nie udostępniaj go nikomu.

Licencja: kod portu pochodzi z projektu Tekken3Remake (PolyForm Noncommercial 1.0.0,
LICENSE.md), wyłącznie do użytku niekomercyjnego. FFmpeg w środku to FFmpeg 8.0.1 na licencji
LGPL 2.1 (zbudowany bez części GPL; źródła: ffmpeg.org, licencje bibliotek w
runtime\ffmpeg\licenses). Tekken jest znakiem towarowym Bandai Namco Entertainment; projekt
nie jest z nią związany.
