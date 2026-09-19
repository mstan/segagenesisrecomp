#include "../../sonicthehedgehog2/sonic2_party.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"line %d: %s\n",__LINE__,#c); exit(1); } } while (0)
static int available = 1;
static int mod_available(void) { return available; }
static const S2Character amy = {"amy","AMY",0,mod_available};
static const S2Character knuckles = {"knuckles","KNUCKLES",0,mod_available};
int main(void)
{
    s2_party_load("party-test-settings.ini");
    s2_party_defaults();
    S2Roster *r = &s2_party.roster;
    CHECK(r->slots == 2 && s2_party.amy_enabled && !s2_party.s3k_enabled);
    CHECK(!strcmp(r->character[0],"sonic") && !strcmp(r->character[1],"tails"));
    CHECK(!strcmp(r->character[2],"none") && !strcmp(r->character[3],"none"));
    CHECK(!s2_roster_validate(r));
    CHECK(!s2_roster_set(r,0,"none") && !s2_roster_set(r,1,"sonic"));
    CHECK(!s2_roster_set(r,2,"amy") && !s2_roster_set(r,99,"sonic"));
    CHECK(s2_character_register(&amy) && s2_character_register(&knuckles));
    CHECK(!s2_character_register(&amy) && !s2_character_register(NULL));
    s2_roster_slots(r,4);
    CHECK(!strcmp(r->character[2],"none"));
    CHECK(s2_roster_cycle(r,2,1) && !strcmp(r->character[2],"amy"));
    CHECK(s2_roster_cycle(r,3,-1) && !strcmp(r->character[3],"knuckles"));
    CHECK(!s2_roster_cycle(r,0,1)); /* every other character already used */
    CHECK(!s2_roster_set(r,1,"amy"));
    CHECK(s2_party_save());
    s2_party_load("party-test-settings.ini");
    CHECK(s2_character_register(&amy) && s2_character_register(&knuckles));
    CHECK(!s2_roster_validate(r) && !strcmp(r->character[3],"knuckles"));
    available = 0;
    CHECK(s2_roster_validate(r));
    CHECK(!strcmp(r->character[2],"none") && !strcmp(r->character[3],"none"));
    CHECK(!s2_roster_set(r,0,"amy"));
    s2_roster_slots(r,1);
    CHECK(!s2_roster_vs_ready(r) && !strcmp(r->character[1],"none"));
    s2_roster_slots(r,2);
    CHECK(!s2_roster_vs_ready(r));
    CHECK(s2_roster_set(r,1,"tails") && s2_roster_vs_ready(r));
    strcpy(r->character[0],"missing");
    strcpy(r->character[1],"sonic");
    CHECK(s2_roster_validate(r) && !strcmp(r->character[0],"tails"));
    s2_roster_slots(r,4);
    strcpy(r->character[0],"missing"); strcpy(r->character[2],"tails");
    CHECK(s2_roster_validate(r) && !strcmp(r->character[0],"sonic"));
    CHECK(!strcmp(r->character[1],"none"));
    strcpy(r->character[3],"sonic");
    CHECK(s2_roster_validate(r) && !strcmp(r->character[3],"none"));
    s2_roster_slots(r,999); CHECK(r->slots == 4);
    s2_roster_slots(r,0); CHECK(r->slots == 1);
    remove("sonic2-party.ini"); /* test-owned sibling fixture in isolated CTest cwd */
    puts("sonic2_party: defaults, uniqueness, cycling, availability, recovery and persistence OK");
    return 0;
}
