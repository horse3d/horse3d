
#include <assert.h>
#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdlib.h>

#include "threading.h"

static uint64_t _last_ticks = 0;
static uint64_t _ticks_offset = 0;
static mutex *_ticks_mutex = NULL;

__attribute__((constructor)) static void _ensureticksmutex() {
    if (!_ticks_mutex)
        _ticks_mutex = mutex_Create();
    assert(_ticks_mutex != NULL);
}

uint64_t datetime_Ticks() {
    _ensureticksmutex();
    mutex_Lock(_ticks_mutex);
    uint64_t ticks = (uint64_t)SDL_GetTicks();
    while (ticks + _ticks_offset < _last_ticks) {
        _ticks_offset += _last_ticks - (ticks + _ticks_offset);
    }
    uint64_t result = (uint64_t)(ticks + _ticks_offset);
    _last_ticks = result;
    mutex_Release(_ticks_mutex);
    return result;
}


void datetime_Sleep(uint64_t ms) {
    SDL_Delay((int)ms);
}
