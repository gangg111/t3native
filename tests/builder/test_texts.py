"""builder/t3builder/texts.py: both languages say the same things, English is the main one."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "builder"))
from t3builder import texts  # noqa: E402

FIELD = re.compile(r"{(\w+)}")


def test_same_keys_and_fields():
    # source: the English table is the reference; every Polish text must fill the same {fields}
    en, pl = texts.TEXTS["en"], texts.TEXTS["pl"]
    assert sorted(en) == sorted(pl)
    for key in en:
        assert sorted(FIELD.findall(en[key])) == sorted(FIELD.findall(pl[key])), key


def test_english_is_the_default():
    # source: decyzja usera 2026-10-22 ("angielski jako główny")
    assert texts.LANGUAGES[0] == "en"
    assert texts.set_language(None) == "en"
    assert texts.set_language("de") == "en"
    assert texts.t("build") == "BUILD APK"
    assert texts.set_language("pl") == "pl"
    assert texts.t("build") == "ZBUDUJ APK"
    assert texts.t("step", index=2, total=5, text="x") == "Krok 2 z 5: x"
    texts.set_language("en")
