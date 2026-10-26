// Host test for main/lang (i18n tables).
// g++ -std=c++17 -Wall -Wextra -Imain -Imain/lang tests/test_lang.cpp main/lang/lang.cpp
//     main/lang/lang_sv.cpp main/lang/lang_en.cpp -o tests/test_lang && tests/test_lang
#include <stdio.h>
#include <string.h>
#include <string>
#include "lang.h"

static int s_pass = 0, s_fail = 0;
#define CHECK(c, ...)                                   \
    do {                                                \
        if (c) ++s_pass;                                \
        else { ++s_fail; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
    } while (0)

// Sequence of printf conversions ("%s", "%04u", "%%" ignored) in a format string.
static std::string placeholders(const char *s) {
    std::string out;
    for (const char *p = s; *p; ++p) {
        if (*p != '%') continue;
        if (p[1] == '%') { ++p; continue; }
        std::string spec = "%";
        ++p;
        while (*p && strchr("0123456789.-+ #lhz", *p)) spec += *p++;
        if (*p) spec += *p;
        else break;
        out += spec;
        out += ',';
    }
    return out;
}

static int count(const char *s, char c) {
    int n = 0;
    for (; *s; ++s) n += *s == c;
    return n;
}

// v0.16.1 regression guard for the "åäö shown as aao" bug. The Swedish UI strings must use real
// UTF-8 å/ä/ö, never the transliterated ASCII spellings (å->a, ä->a, ö->o) that slipped into some
// v0.16.0 content. Key insight: in correct UTF-8 a multibyte å/ä/ö splits a word into *separate*
// ASCII runs ("från" -> "fr","n"), so a telltale token below can only appear as one whole ASCII
// run when the word was actually transliterated. That is what keeps this free of false positives.
static bool isAsciiLetter(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
static bool hasTranslitWord(const char *s, const char *word) {
    const size_t wl = strlen(word);
    for (const char *p = s; *p;) {
        if (!isAsciiLetter(*p)) { ++p; continue; }
        const char *start = p;
        while (isAsciiLetter(*p)) ++p;
        if ((size_t)(p - start) != wl) continue;
        bool eq = true;
        for (size_t i = 0; i < wl; ++i) {
            char a = start[i];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (a != word[i]) { eq = false; break; }
        }
        if (eq) return true;
    }
    return false;
}
// Curated, unambiguous transliterations of Swedish words that contain å/ä/ö. Each is validated to
// produce zero false positives against the corrected tables/story (see test run).
static const char *const TRANSLIT[] = {
    "nagon", "nagot", "nagra", "fran", "maste", "varld", "varlden", "varldar", "varlden",
    "aventyr", "aventyret", "aventyren", "dorr", "dorren", "dromm", "dromde", "drommar",
    "drommen", "drommaren", "farg", "farger", "oppna", "oppen", "oppnas", "oppnar", "oppet",
    "hjarta", "hjartat", "forsta", "forstar", "fardig", "fardiga", "valja", "valjer",
    "sprak", "vag", "vagen", "vagar", "bade", "lagret", "lager",
    "kopman", "kopmannen", "broder", "broderna", "sanker", "karvar", "karve", "manen",
    "stjarnor", "aven", "hogt", "langt", "sjalv", "val komna", "valkommen", "far jag se",
};
static int scanTranslit(const char *label, const char *sv) {
    int hits = 0;
    for (const char *w : TRANSLIT)
        if (hasTranslitWord(sv, w)) {
            ++hits;
            printf("FAIL translit %s: token '%s' in \"%s\"\n", label, w, sv);
        }
    return hits;
}

int main() {
    const int N = (int)Str::COUNT;
    CHECK(N > 150, "Str::COUNT=%d", N);
    CHECK(Lang::count() >= 2, "languages=%d", Lang::count());
    CHECK(Lang::indexOf("sv") == 0, "sv must be index 0 (base language)");
    CHECK(Lang::indexOf("en") >= 0, "en missing");
    CHECK(Lang::indexOf("xx") == -1, "unknown code");
    CHECK(Lang::indexOf(nullptr) == -1, "null code");

    // Codes/names well-formed and unique.
    for (int l = 0; l < Lang::count(); ++l) {
        CHECK(Lang::code(l) && strlen(Lang::code(l)) >= 2 && strlen(Lang::code(l)) < (size_t)Lang::CODE_CAP,
              "code %d", l);
        CHECK(Lang::name(l) && *Lang::name(l), "name %d", l);
        for (int k = 0; k < l; ++k) CHECK(strcmp(Lang::code(l), Lang::code(k)) != 0, "dup code %d/%d", l, k);
    }

    // Every id has a non-empty text in every language; placeholders and line breaks match Swedish.
    for (int l = 0; l < Lang::count(); ++l) {
        for (int i = 0; i < N; ++i) {
            const Str id = (Str)i;
            const char *t = Lang::lookup(l, id);
            CHECK(t && *t, "[%s] %s missing", Lang::code(l), Lang::idName(id));
            if (!t || l == 0) continue;
            const char *sv = Lang::lookup(0, id);
            if (!sv) continue;
            CHECK(placeholders(sv) == placeholders(t), "[%s] %s placeholders '%s' vs '%s'", Lang::code(l),
                  Lang::idName(id), placeholders(sv).c_str(), placeholders(t).c_str());
            CHECK(count(sv, '\n') == count(t, '\n'), "[%s] %s line breaks differ", Lang::code(l), Lang::idName(id));
        }
    }
    // v0.16.1: no Swedish UI string may use a transliterated spelling of an å/ä/ö word.
    {
        int translitHits = 0;
        for (int i = 0; i < N; ++i) {
            const char *sv = Lang::lookup(0, (Str)i);
            if (sv) translitHits += scanTranslit(Lang::idName((Str)i), sv);
        }
        CHECK(translitHits == 0, "%d transliterated Swedish tokens found in lang table", translitHits);
    }

    CHECK(Lang::lookup(99, Str::BTN_SAVE) == nullptr, "bad lang index");
    CHECK(Lang::lookup(0, Str::COUNT) == nullptr, "COUNT is not a string");
    CHECK(strcmp(Lang::idName(Str::BTN_SAVE), "BTN_SAVE") == 0, "idName");

    // Switching language.
    CHECK(strcmp(Lang::currentLang(), "sv") == 0, "default sv, got %s", Lang::currentLang());
    CHECK(Lang::setLang("en"), "setLang en");
    CHECK(strcmp(Lang::currentLang(), "en") == 0, "current en");
    CHECK(strcmp(tr(Str::BTN_SAVE), Lang::lookup(Lang::indexOf("en"), Str::BTN_SAVE)) == 0, "tr en");
    CHECK(!Lang::setLang("xx"), "unknown code returns false");
    CHECK(strcmp(Lang::currentLang(), "sv") == 0, "unknown -> sv");
    CHECK(strcmp(tr(Str::BTN_SAVE), Lang::lookup(0, Str::BTN_SAVE)) == 0, "tr sv");
    CHECK(!Lang::setLang(nullptr), "null code returns false");
    CHECK(Lang::valid("sv") && Lang::valid("en") && !Lang::valid("") && !Lang::valid(nullptr), "valid()");

    // Invalid ids.
    CHECK(tr(Str::COUNT) && *tr(Str::COUNT) == 0, "tr(COUNT) == \"\"");
    CHECK(tr((Str)60000) && *tr((Str)60000) == 0, "tr(out of range) == \"\"");

    // trp: prefix + text + suffix, rotating buffers stay valid for a few calls.
    std::string expect = std::string(">> ") + tr(Str::BTN_SAVE) + " <<";
    const char *a = trp(">> ", Str::BTN_SAVE, " <<");
    const char *b = trp("", Str::BTN_CANCEL);
    CHECK(expect == a, "trp '%s'", a);
    CHECK(strcmp(b, tr(Str::BTN_CANCEL)) == 0, "trp no prefix");
    CHECK(a != b, "trp rotates buffers");
    Lang::setLang("en");
    CHECK(std::string("x") + tr(Str::BTN_SAVE) == trp("x", Str::BTN_SAVE), "trp en");
    Lang::setLang("sv");

    printf("test_lang: %d strings, %d languages, %d passed, %d failed\n", N, Lang::count(), s_pass, s_fail);
    return s_fail ? 1 : 0;
}
