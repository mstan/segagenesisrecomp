#include "engine_worker.h"
#include <stdlib.h>
#include <stdio.h>
#ifndef GENESIS_ENGINE_WORKERS
#define GENESIS_ENGINE_WORKERS 1
#endif
#if GENESIS_ENGINE_WORKERS
#include <SDL2/SDL.h>
struct GenesisWorker {
    SDL_Thread *thread;SDL_mutex *mutex;SDL_cond *condition;
    GenesisWorkFunction task;void *context;int stop;
    uint64_t completed;
};
static int worker_main(void *user)
{
    GenesisWorker *w=(GenesisWorker *)user;
    SDL_LockMutex(w->mutex);
    for(;;){
        while(!w->task && !w->stop)SDL_CondWait(w->condition,w->mutex);
        if(w->stop)break;
        GenesisWorkFunction task=w->task;void *context=w->context;
        SDL_UnlockMutex(w->mutex);task(context);SDL_LockMutex(w->mutex);
        ++w->completed;w->task=NULL;w->context=NULL;
        SDL_CondBroadcast(w->condition);
    }
    SDL_UnlockMutex(w->mutex);return 0;
}
GenesisWorker *genesis_worker_create(void)
{
    if(SDL_GetCPUCount()<2)return NULL;
    GenesisWorker *w=(GenesisWorker *)calloc(1,sizeof *w);if(!w)return NULL;
    w->mutex=SDL_CreateMutex();w->condition=SDL_CreateCond();
    if(w->mutex && w->condition)w->thread=SDL_CreateThread(worker_main,"Genesis engine work",w);
    if(!w->thread){if(w->condition)SDL_DestroyCond(w->condition);if(w->mutex)SDL_DestroyMutex(w->mutex);free(w);return NULL;}
    return w;
}
void genesis_worker_join(GenesisWorker *w)
{
    if(!w)return;SDL_LockMutex(w->mutex);
    while(w->task)SDL_CondWait(w->condition,w->mutex);
    SDL_UnlockMutex(w->mutex);
}
int genesis_worker_start(GenesisWorker *w,GenesisWorkFunction task,void *context)
{
    if(!task)return 0;
    if(!w){task(context);return 0;}
    genesis_worker_join(w);SDL_LockMutex(w->mutex);
    w->context=context;w->task=task;SDL_CondSignal(w->condition);
    SDL_UnlockMutex(w->mutex);return 1;
}
uint64_t genesis_worker_completed(const GenesisWorker *w)
{
    if(!w)return 0;
    SDL_LockMutex(w->mutex);uint64_t count=w->completed;SDL_UnlockMutex(w->mutex);
    return count;
}
void genesis_worker_destroy(GenesisWorker *w)
{
    if(!w)return;genesis_worker_join(w);SDL_LockMutex(w->mutex);
    w->stop=1;SDL_CondSignal(w->condition);SDL_UnlockMutex(w->mutex);
    SDL_WaitThread(w->thread,NULL);SDL_DestroyCond(w->condition);SDL_DestroyMutex(w->mutex);free(w);
}
#else
struct GenesisWorker {int unused;};
GenesisWorker *genesis_worker_create(void){return NULL;}
int genesis_worker_start(GenesisWorker *w,GenesisWorkFunction task,void *context)
{(void)w;if(task)task(context);return 0;}
void genesis_worker_join(GenesisWorker *w){(void)w;}
void genesis_worker_destroy(GenesisWorker *w){(void)w;}
uint64_t genesis_worker_completed(const GenesisWorker *w){(void)w;return 0;}
#endif
