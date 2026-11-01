/* Durable-snapshot completion and producer-callback contract. */
#define main host_writers_original_main
#include "host_writers_test.c"
#undef main

static unsigned completed_count;
static unsigned char expected_payload[64];
static int callback_context;
static void completed_check(const char *path, const void *data, size_t n, void *context)
{
    CHECK(SDL_ThreadID() == main_thread);
    CHECK(context == &callback_context);
    CHECK(n == sizeof expected_payload);
    CHECK(!strcmp(path, "immutable-progress.srm"));
    CHECK(!memcmp(data, expected_payload, n));
    CHECK(!memcmp(data, durable, n));
    ++completed_count;
}
static GenesisPersistWriter *observed_persist(void)
{
    GenesisPersistWriter *p = make_persist();
    genesis_persist_on_completed(p, completed_check, &callback_context);
    return p;
}
int main(void)
{
    CHECK(SDL_Init(0) == 0); main_thread = SDL_ThreadID();
    init_gate(&persist_gate);
    unsigned char live[64], newer[64];
    GenesisPersistWriter *p = observed_persist();

    /* A force request must actually replace equal bytes and notify once. */
    memset(live, 'A', sizeof live); memcpy(expected_payload, live, sizeof live);
    unsigned before = writes;
    CHECK(genesis_persist_submit_force(p, live) == 1);
    CHECK(genesis_persist_wait(p));
    CHECK(writes == before + 1 && completed_count == 1);
    CHECK(genesis_persist_submit(p, live) == 1);
    CHECK(genesis_persist_wait(p));
    CHECK(writes == before + 1 && completed_count == 1);

    /* Busy polling cannot acknowledge durability; waiting drains only B. */
    memset(live, 'B', sizeof live); memcpy(expected_payload, live, sizeof live);
    memset(newer, 'C', sizeof newer);
#if GENESIS_ENGINE_WORKERS
    SDL_AtomicSet(&persist_gate.enabled, 1);
#endif
    CHECK(genesis_persist_submit_force(p, live) == 1);
    memset(live, 'Z', sizeof live);
#if GENESIS_ENGINE_WORKERS
    CHECK(p->worker != NULL); wait_started(&persist_gate);
    CHECK(genesis_persist_poll(p) && p->inflight && completed_count == 1);
    CHECK(genesis_persist_submit_force(p, newer) == 0);
    SDL_Thread *release = release_thread(&persist_gate);
#endif
    CHECK(genesis_persist_wait(p));
#if GENESIS_ENGINE_WORKERS
    SDL_WaitThread(release, NULL);
#endif
    CHECK(completed_count == 2 && durable[0] == 'B' && p->committed[0] == 'B');
    CHECK(genesis_persist_wait(p) && completed_count == 2);

    memcpy(expected_payload, newer, sizeof newer);
    CHECK(genesis_persist_submit_force(p, newer) == 1);
    CHECK(genesis_persist_wait(p)); CHECK(completed_count == 3);

    /* Destroy notifies the accepted D image; caller's later E is unsubmitted. */
    memset(live, 'D', sizeof live); memcpy(expected_payload, live, sizeof live);
#if GENESIS_ENGINE_WORKERS
    SDL_AtomicSet(&persist_gate.enabled, 1);
#endif
    CHECK(genesis_persist_submit_force(p, live) == 1); memset(live, 'E', sizeof live);
#if GENESIS_ENGINE_WORKERS
    wait_started(&persist_gate); release = release_thread(&persist_gate);
#endif
    CHECK(genesis_persist_destroy(p));
#if GENESIS_ENGINE_WORKERS
    SDL_WaitThread(release, NULL);
#endif
    CHECK(completed_count == 4 && durable[0] == 'D');

    /* A failed write never notifies or advances the committed image. */
    p = observed_persist(); SDL_AtomicSet(&fail_write, 1);
    memset(live, 'F', sizeof live); memcpy(expected_payload, live, sizeof live);
    int accepted = genesis_persist_submit_force(p, live);
    CHECK(accepted == 1 || accepted == -1);
    CHECK(!genesis_persist_wait(p));
    CHECK(completed_count == 4 && durable[0] == 'A' && p->committed[0] == 'A');
    CHECK(!genesis_persist_destroy(p)); SDL_AtomicSet(&fail_write, 0);

    SDL_DestroySemaphore(persist_gate.started); SDL_DestroySemaphore(persist_gate.allow);
    printf("PASS callback-force-wait workers=%d checks=%u callbacks=%u\n",
           GENESIS_ENGINE_WORKERS, checks, completed_count);
    SDL_Quit(); return 0;
}
