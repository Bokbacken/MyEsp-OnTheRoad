// lang.cpp - language table, lookup and fallback (see lang.h). No ESP-IDF dependencies.
#include "lang.h"

#include <stdio.h>
#include <string.h>

namespace Lang {

extern const LangDef DEF_SV;
extern const LangDef DEF_EN;

// Index 0 is the base language (complete; used as the fallback for missing entries).
static const LangDef *const LANGS[] = {&DEF_SV, &DEF_EN};
static constexpr int LANG_COUNT = (int)(sizeof(LANGS) / sizeof(LANGS[0]));
static_assert(LANG_COUNT <= MAX_LANGS, "raise Lang::MAX_LANGS");

static constexpr int N = (int)Str::COUNT;

// Resolved tables: one per language, built on first use (pointer stores only, so a reader on
// another task - the web server - always sees either the old or the new pointer).
static const char *s_resolved[LANG_COUNT][N];
static bool s_built = false;
static int s_cur = 0;

static void build()
{
    if (s_built) return;
    for (int l = 0; l < LANG_COUNT; l++) {
        for (int i = 0; i < N; i++) s_resolved[l][i] = nullptr;
        const LangDef *d = LANGS[l];
        for (size_t k = 0; k < d->count; k++) {
            int i = (int)d->entries[k].id;
            if (i >= 0 && i < N && d->entries[k].text) s_resolved[l][i] = d->entries[k].text;
        }
    }
    for (int l = 1; l < LANG_COUNT; l++)
        for (int i = 0; i < N; i++)
            if (!s_resolved[l][i] || !s_resolved[l][i][0]) s_resolved[l][i] = s_resolved[0][i];
    for (int i = 0; i < N; i++)
        if (!s_resolved[0][i]) s_resolved[0][i] = "";
    s_built = true;
}

int count() { return LANG_COUNT; }
const char *code(int i) { return i >= 0 && i < LANG_COUNT ? LANGS[i]->code : ""; }
const char *name(int i) { return i >= 0 && i < LANG_COUNT ? LANGS[i]->name : ""; }

int indexOf(const char *c)
{
    if (!c) return -1;
    for (int i = 0; i < LANG_COUNT; i++)
        if (strcmp(LANGS[i]->code, c) == 0) return i;
    return -1;
}

bool valid(const char *c) { return indexOf(c) >= 0; }

bool setLang(const char *c)
{
    build();
    int i = indexOf(c);
    s_cur = i >= 0 ? i : 0;
    return i >= 0;
}

const char *currentLang() { return LANGS[s_cur]->code; }

const char *lookup(int lang, Str id)
{
    if (lang < 0 || lang >= LANG_COUNT) return nullptr;
    const LangDef *d = LANGS[lang];
    for (size_t k = 0; k < d->count; k++)
        if (d->entries[k].id == id) return d->entries[k].text;
    return nullptr;
}

const char *idName(Str id)
{
    static const char *const NAMES[] = {
#define LANG_X_NAME(x) #x,
        LANG_STR_IDS(LANG_X_NAME)
#undef LANG_X_NAME
    };
    int i = (int)id;
    return i >= 0 && i < N ? NAMES[i] : "?";
}

}  // namespace Lang

const char *tr(Str id)
{
    Lang::build();
    int i = (int)id;
    if (i < 0 || i >= Lang::N) return "";
    return Lang::s_resolved[Lang::s_cur][i];
}

const char *trp(const char *prefix, Str id, const char *suffix)
{
    static char bufs[8][128];
    static unsigned next = 0;
    char *b = bufs[next++ & 7];
    snprintf(b, sizeof(bufs[0]), "%s%s%s", prefix ? prefix : "", tr(id), suffix ? suffix : "");
    return b;
}
