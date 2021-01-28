// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_THREADING_H_
#define HORSE64_THREADING_H_

#include <stdint.h>

typedef struct mutex mutex;

typedef struct semaphore semaphore;

typedef struct threadinfo thread;


semaphore *semaphore_Create(int value);

void semaphore_Wait(semaphore *s);

void semaphore_Post(semaphore *s);

void semaphore_Destroy(semaphore *s);


mutex *mutex_Create();

void mutex_Lock(mutex *m);

int mutex_TryLock(mutex *m);

void mutex_Release(mutex *m);

void mutex_Destroy(mutex *m);

int mutex_IsLocked(mutex *m);  // ONLY FOR DEBUG PURPOSES (slow!)


thread *thread_Spawn(
    void (*func)(void *userdata),
    void *userdata
);

#define THREAD_PRIO_LOW 1
#define THREAD_PRIO_NORMAL 2
#define THREAD_PRIO_HIGH 3

thread *thread_SpawnWithPriority(
    int priority,
    void (*func)(void *userdata), void *userdata
);

uint64_t thread_GetId(thread *t);

void thread_Detach(thread *t);

void thread_Join(thread *t);

int thread_InMainThread();

uint64_t thread_GetOurThreadId();

int32_t threadlocalstorage_RegisterType(
    uint64_t bytes, void (*clearHandler)(
        uint32_t storage_type_id, void *storageptr,
        uint64_t storage_bytes
    )
);

void *threadlocalstorage_GetByType(int32_t type);

typedef struct threadevent threadevent;
typedef struct h64socket h64socket;

threadevent *threadevent_Create();

h64socket *threadevent_WaitForSocket(threadevent *te);

void threadevent_Set(threadevent *te);

int threadevent_PollIsSet(threadevent *te, int unsetifset);

void threadevent_Free(threadevent *te);

void threadevent_Unset(threadevent *te);

int threadevent_WaitUntilSet(
    threadevent *te, uint64_t timeout_ms,
    int unsetifset
);

void threadevent_FlushWakeUpEvents(threadevent *te);


#endif  // HORSE64_THREADING_H_
