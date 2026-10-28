### Jak zbudować APK z własnych zrzutów

English: [README](../README.md#how-to-build-the-apk-from-your-own-discs).

![Kompilator Tekken 3](builder.png)

1. **Zrzuć płytę PlayStation** jako BIN/CUE (nie ISO, traci muzykę i filmy), na przykład
   programem ImgBurn: „Create image file from disc”. Wymagana: Tekken 3 (Japan), SLPS-01300.
   Opcjonalnie: Tekken 3 (USA), SLUS-00402, dla angielskich tekstów.
2. **Zrzuć automat.** Odczytaj układy ROM swojej płyty automatu Tekken 3 (Namco
   System 12) programatorem EPROM i zapisz je jako zestaw MAME `tekken3.zip`. Dodaje sceny
   i modele postaci z automatu.
3. **Zbuduj.** Uruchom `Tekken3Builder.exe` z
   [najnowszego wydania](https://github.com/gangg111/t3native/releases/latest), wskaż zrzuty
   i kliknij ZBUDUJ APK.
4. **Zainstaluj** `Tekken3.apk` na telefonie (Android 8.0 lub nowszy). Zachowaj folder `key`
   obok kompilatora, żeby kolejny APK wszedł jako aktualizacja.

APK zawiera dane z Twoich zrzutów: tylko do prywatnego użytku.
