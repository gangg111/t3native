"""The builder's words in English (the main language) and Polish: `t(key, **values)` in the
language set by `set_language` (English when none or an unknown one is set)."""
from __future__ import annotations

LANGUAGES = ("en", "pl")

TEXTS = {
    "en": {
        "title": "Tekken 3 Builder",
        "jp": "PlayStation disc, Japanese release",
        "jp_hint": "Tekken 3 (Japan) (Rev 1) or (Japan), SLPS-01300: folder of the .cue and .bin files",
        "usa": "PlayStation disc, USA release",
        "usa_hint": "Tekken 3 (USA), SLUS-00402: English texts and move lists",
        "arcade": "Arcade (MAME set tekken3.zip)",
        "arcade_hint": "MAME set tekken3 (World ver. E1): the arcade's stages and models",
        "out": "Folder for the finished APK",
        "out_hint": "output folder",
        "optional": "optional",
        "variant": "Network mode for multiplayer",
        "variant_lan": "LOCAL WI-FI",
        "variant_direct": "WI-FI DIRECT",
        "choose": "Choose",
        "build": "BUILD APK",
        "stop": "Stop",
        "open": "Open the APK's folder",
        "pick_disc": "Folder of the disc image",
        "pick_arcade": "MAME arcade set",
        "mame_set": "MAME set",
        "pick_out": "Folder for the APK",
        "chd": "CHD images are not supported: extract them to .cue/.bin first (chdman extractcd from MAME).",
        "no_image": "No disc image in {path} (.cue with its .bin files, .ecm or .iso).",
        "need_jp": "Choose the image of the Japanese PlayStation disc.",
        "no_arcade": "The arcade set file does not exist.",
        "starting": "Starting…",
        "step": "Step {index} of {total}: {text}",
        "done": "Done in {minutes} min: {path}",
        "failed": "The build failed: the reason is at the end of the log.",
        "stopped": "Stopped.",
        "checking": "checking…",
        "is_usa": "the USA release: put it in the USA field",
        "is_japanese": "a Japanese release: put it in the first field",
        "unknown_disc": "not a supported Tekken 3 disc",
        "bad_arcade": "not MAME's tekken3 set (World ver. E1)",
        "fix_field": "{field}: {problem}.",
        # The pipeline's steps and errors.
        "s_disc": "Converting the PS1 disc (graphics, sound, music, movies)",
        "s_arcade": "Converting the arcade (stages and character models)",
        "s_fonts": "The game's fonts",
        "s_key": "Signing key",
        "s_apk": "Packing and signing the APK",
        "e_convert": "the disc conversion failed (details in the log above)",
        "e_ffmpeg": "FFmpeg is missing from the builder's folder",
        "e_arcade": "the arcade set gave no stage",
        "e_fonts": "fonts: the sheets differ from the converter's",
        "e_align": "unaligned APK entries: {entries}",
        "key_new": "new signing key: {path}",
        "key_kept": "saved signing key: {path}",
        "arcade_done": "arcade: {stages} stages, {models} character models",
        "apk_done": "{files} data files, APK {size} MB",
        "error": "ERROR: {error}",
    },
    "pl": {
        "title": "Kompilator Tekken 3",
        "jp": "Płyta PlayStation, wersja japońska",
        "jp_hint": "Tekken 3 (Japan) (Rev 1) albo (Japan), SLPS-01300: folder z plikami .cue i .bin",
        "usa": "Płyta PlayStation, wersja USA",
        "usa_hint": "Tekken 3 (USA), SLUS-00402: angielskie teksty i listy ruchów",
        "arcade": "Automat (zestaw MAME tekken3.zip)",
        "arcade_hint": "zestaw MAME tekken3 (World ver. E1): sceny i modele z automatu",
        "out": "Folder na gotowy plik APK",
        "out_hint": "folder docelowy",
        "optional": "opcjonalnie",
        "variant": "Tryb sieciowy do gry wieloosobowej",
        "variant_lan": "SIEĆ WI-FI",
        "variant_direct": "WI-FI DIRECT",
        "choose": "Wybierz",
        "build": "ZBUDUJ APK",
        "stop": "Przerwij",
        "open": "Otwórz folder z APK",
        "pick_disc": "Folder z obrazem płyty",
        "pick_arcade": "Zestaw MAME automatu",
        "mame_set": "Zestaw MAME",
        "pick_out": "Folder na plik APK",
        "chd": "Obrazy .chd nie są obsługiwane: rozpakuj je do .cue/.bin (chdman extractcd z pakietu MAME).",
        "no_image": "W {path} nie ma obrazu płyty (.cue z plikami .bin, .ecm albo .iso).",
        "need_jp": "Wskaż obraz japońskiej płyty PlayStation.",
        "no_arcade": "Nie ma pliku zestawu automatu.",
        "starting": "Start…",
        "step": "Krok {index} z {total}: {text}",
        "done": "Gotowe w {minutes} min: {path}",
        "failed": "Budowa nie powiodła się: powód jest na końcu logu.",
        "stopped": "Przerwano.",
        "checking": "sprawdzam…",
        "is_usa": "to wydanie USA: wskaż je w polu USA",
        "is_japanese": "to wydanie japońskie: wskaż je w pierwszym polu",
        "unknown_disc": "to nie jest obsługiwana płyta Tekken 3",
        "bad_arcade": "to nie jest zestaw MAME tekken3 (World ver. E1)",
        "fix_field": "{field}: {problem}.",
        "s_disc": "Konwersja płyty PS1 (grafika, dźwięk, muzyka, filmy)",
        "s_arcade": "Konwersja automatu (sceny i modele postaci)",
        "s_fonts": "Czcionki gry",
        "s_key": "Klucz podpisu",
        "s_apk": "Składanie i podpisywanie APK",
        "e_convert": "konwersja płyty nie powiodła się (szczegóły w logu powyżej)",
        "e_ffmpeg": "brak FFmpeg w folderze kompilatora",
        "e_arcade": "zestaw automatu nie dał żadnej sceny",
        "e_fonts": "czcionki: arkusze różnią się od konwertera",
        "e_align": "niewyrównane wpisy APK: {entries}",
        "key_new": "nowy klucz podpisu: {path}",
        "key_kept": "zapisany klucz podpisu: {path}",
        "arcade_done": "automat: {stages} scen, {models} modeli postaci",
        "apk_done": "{files} plików danych, APK {size} MB",
        "error": "BŁĄD: {error}",
    },
}

_language = "en"


def set_language(language: str | None) -> str:
    """Sets the language (English for anything not in LANGUAGES); returns the one set."""
    global _language
    _language = language if language in LANGUAGES else "en"
    return _language


def language() -> str:
    return _language


def t(key: str, **values) -> str:
    return TEXTS[_language][key].format(**values)
