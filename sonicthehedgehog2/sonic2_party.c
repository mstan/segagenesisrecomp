#include "sonic2_party.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

S2PartyConfig s2_party;
static const S2Character *s_characters[S2_MAX_CHARACTERS];
static unsigned s_count;
static char s_path[1200] = "sonic2-party.ini";
static char s_error[160];
static int native_available(void) { return 1; }
static const S2Character s_sonic = {"sonic", "SONIC", 1, native_available};
static const S2Character s_tails = {"tails", "TAILS", 2, native_available};

int s2_character_register(const S2Character *definition)
{
    if (!definition || !definition->id || !definition->label || !definition->available ||
        !*definition->id || strlen(definition->id) >= S2_CHARACTER_ID_SIZE ||
        !strcmp(definition->id, "none") || s2_character_find(definition->id) ||
        s_count == S2_MAX_CHARACTERS) return 0;
    s_characters[s_count++] = definition;
    return 1;
}
const S2Character *s2_character_find(const char *id)
{
    if (id) for (unsigned i = 0; i < s_count; ++i)
        if (!strcmp(s_characters[i]->id, id)) return s_characters[i];
    return NULL;
}
const S2Character *s2_character_at(unsigned index) { return index < s_count ? s_characters[index] : NULL; }
unsigned s2_character_count(void) { return s_count; }

void s2_party_defaults(void)
{
    memset(&s2_party, 0, sizeof s2_party);
    s2_party.roster.slots = 2;
    strcpy(s2_party.roster.character[0], "sonic");
    strcpy(s2_party.roster.character[1], "tails");
    strcpy(s2_party.roster.character[2], "none");
    strcpy(s2_party.roster.character[3], "none");
    s2_party.amy_enabled = 1;
    s_count = 0;
    s2_character_register(&s_sonic);
    s2_character_register(&s_tails);
    s_error[0] = 0;
}

static int selectable(const S2Roster *roster, unsigned player, const char *id)
{
    if (player >= roster->slots || player >= S2_MAX_PLAYERS || !id) return 0;
    if (!strcmp(id, "none")) return player != 0;
    const S2Character *character = s2_character_find(id);
    if (!character || !character->available()) return 0;
    for (unsigned i = 0; i < S2_MAX_PLAYERS; ++i)
        if (i != player && !strcmp(roster->character[i], id)) return 0;
    return 1;
}
int s2_roster_set(S2Roster *roster, unsigned player, const char *id)
{
    if (!roster || !selectable(roster, player, id)) return 0;
    snprintf(roster->character[player], S2_CHARACTER_ID_SIZE, "%s", id);
    return 1;
}
int s2_roster_cycle(S2Roster *roster, unsigned player, int direction)
{
    if (!roster || player >= roster->slots || player >= S2_MAX_PLAYERS || !direction) return 0;
    unsigned current = 0, choices = s_count + 1;
    for (unsigned i = 0; i < s_count; ++i)
        if (!strcmp(roster->character[player], s_characters[i]->id)) current = i + 1;
    for (unsigned step = 1; step < choices; ++step) {
        unsigned candidate = (current + (direction > 0 ? step : choices - step)) % choices;
        const char *id = candidate ? s_characters[candidate - 1]->id : "none";
        if (s2_roster_set(roster, player, id)) return 1;
    }
    return 0;
}
void s2_roster_slots(S2Roster *roster, unsigned slots)
{
    if (!roster) return;
    roster->slots = slots < 1 ? 1 : slots > S2_MAX_PLAYERS ? S2_MAX_PLAYERS : slots;
    for (unsigned i = roster->slots; i < S2_MAX_PLAYERS; ++i) strcpy(roster->character[i], "none");
}
int s2_roster_validate(S2Roster *roster)
{
    if (!roster) return 0;
    S2Roster before = *roster;
    s2_roster_slots(roster, roster->slots);
    // Repair unavailable selections and duplicates in slot order. Never assign
    // a mod character implicitly; only P1 may need a native fallback.
    for (unsigned i = 0; i < roster->slots; ++i) {
        const S2Character *c = s2_character_find(roster->character[i]);
        int valid = c && c->available();
        for (unsigned j = 0; j < i; ++j)
            if (!strcmp(roster->character[i], roster->character[j])) valid = 0;
        if (!valid) strcpy(roster->character[i], "none");
    }
    if (!strcmp(roster->character[0], "none")) {
        if (!s2_roster_set(roster, 0, "sonic") && !s2_roster_set(roster, 0, "tails")) {
            // Both native characters are in companion slots: reclaim Sonic.
            for (unsigned i = 1; i < roster->slots; ++i)
                if (!strcmp(roster->character[i], "sonic")) strcpy(roster->character[i], "none");
            s2_roster_set(roster, 0, "sonic");
        }
    }
    return memcmp(&before, roster, sizeof before) != 0;
}
int s2_roster_vs_ready(const S2Roster *roster)
{
    return roster && roster->slots >= 2 && strcmp(roster->character[0], "none") &&
        strcmp(roster->character[1], "none") && selectable(roster, 0, roster->character[0]) &&
        selectable(roster, 1, roster->character[1]);
}

void s2_party_load(const char *settings_path)
{
    s2_party_defaults();
    snprintf(s_path, sizeof s_path, "%s", settings_path ? settings_path : "settings.ini");
    char *slash = strrchr(s_path, '/'), *backslash = strrchr(s_path, '\\');
    char *base = slash && (!backslash || slash > backslash) ? slash + 1 : backslash ? backslash + 1 : s_path;
    snprintf(base, sizeof s_path - (size_t)(base - s_path), "sonic2-party.ini");
    FILE *file = fopen(s_path, "rb");
    if (!file) return;
    char line[1200];
    while (fgets(line, sizeof line, file)) {
        char *value = strchr(line, '=');
        if (!value) continue;
        *value++ = 0;
        value[strcspn(value, "\r\n")] = 0;
        if (!strcmp(line, "slots")) s2_party.roster.slots = (unsigned)strtoul(value, NULL, 10);
        else if (!strcmp(line, "amy_enabled")) s2_party.amy_enabled = atoi(value) == 1;
        else if (!strcmp(line, "s3k_enabled")) s2_party.s3k_enabled = atoi(value) == 1;
        else if (!strcmp(line, "save_menu_enabled")) s2_party.save_menu_enabled = atoi(value) == 1;
        else if (!strcmp(line, "amy_path")) snprintf(s2_party.amy_path, sizeof s2_party.amy_path, "%s", value);
        else if (!strcmp(line, "s3k_path")) snprintf(s2_party.s3k_path, sizeof s2_party.s3k_path, "%s", value);
        else if (!strcmp(line, "campaign_path")) snprintf(s2_party.campaign_path, sizeof s2_party.campaign_path, "%s", value);
        else for (unsigned i = 0; i < S2_MAX_PLAYERS; ++i) {
            char key[16]; snprintf(key, sizeof key, "player%u", i + 1);
            if (!strcmp(line, key)) snprintf(s2_party.roster.character[i], S2_CHARACTER_ID_SIZE, "%s", value);
        }
    }
    fclose(file);
    // Resource-backed characters register before the caller validates this roster.
}
int s2_party_save(void)
{
    char temporary[1220];
    snprintf(temporary, sizeof temporary, "%s.tmp", s_path);
    FILE *file = fopen(temporary, "wb");
    if (!file) { strcpy(s_error, "Unable to write roster settings"); return 0; }
    fprintf(file, "version=1\nslots=%u\namy_enabled=%d\ns3k_enabled=%d\namy_path=%s\ns3k_path=%s\n",
        s2_party.roster.slots, s2_party.amy_enabled, s2_party.s3k_enabled, s2_party.amy_path, s2_party.s3k_path);
    for (unsigned i = 0; i < S2_MAX_PLAYERS; ++i) fprintf(file, "player%u=%s\n", i + 1, s2_party.roster.character[i]);
    fprintf(file, "save_menu_enabled=%d\n", s2_party.save_menu_enabled);
    fprintf(file, "campaign_path=%s\n", s2_party.campaign_path);
    int ok = !ferror(file);
    if (fclose(file)) ok = 0;
#ifdef _WIN32
    if (ok) ok = MoveFileExA(temporary, s_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    if (ok) ok = rename(temporary, s_path) == 0;
#endif
    if (!ok) { strcpy(s_error, "Unable to save roster settings"); return 0; }
    s_error[0] = 0;
    return 1;
}
const char *s2_party_error(void) { return s_error; }
