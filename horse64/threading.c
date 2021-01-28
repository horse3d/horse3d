// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#ifndef ISWIN
#if defined(WIN32) || defined(_WIN32) || defined(_WIN64) || defined(__WIN32__)
#define ISWIN
#endif
#endif

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <inttypes.h>
#if defined(_WIN32) || defined(_WIN64)
#include <process.h>
#include <winsock2.h>
#include <windows.h>
#else
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <sched.h>
#include <semaphore.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#endif
#if defined(__linux__) || defined(linux) || defined(__linux)
#include <linux/sched.h>
#endif

#include "datetime.h"
#include "nonlocale.h"
#include "secrandom.h"
#include "sockets.h"
#include "threading.h"


#ifndef ISWIN
static pthread_t mainThread;
#else
static DWORD mainThread;
#endif

static int _markedmainthread = 0;

__attribute__((constructor)) void thread_MarkAsMainThread(void) {
    if (_markedmainthread)
        return;
    // Mark current thread as main thread
    _markedmainthread = 1;
#ifndef ISWIN
    mainThread = pthread_self();
#else
    mainThread = GetCurrentThreadId();
#endif
}


typedef struct mutex {
#ifdef ISWIN
    HANDLE m;
#else
    pthread_mutex_t m;
#endif
} mutex;


typedef struct semaphore {
#ifdef ISWIN
    HANDLE s;
#else
#if defined(__APPLE__) || defined(__OSX__)
    sem_t *s;
    char *sname;
#else
    sem_t s;
#endif
#endif
} semaphore;


typedef struct threadinfo {
#ifdef ISWIN
    HANDLE t;
#else
    pthread_t t;
#endif
    semaphore *_setid_semaphore;
    uint64_t id;
} thread;

#define IDTOSTORAGEMAP_BUCKETS 64

typedef struct threadlocalstoragetype {
    uint64_t bytes;
    void (*clearHandler)(
        uint32_t storage_type_id, void *storageptr,
        uint64_t storage_bytes
    );
    int idtostoragemap_bucketitems[IDTOSTORAGEMAP_BUCKETS];
    uint64_t *idtostoragemap_bucketidlist[IDTOSTORAGEMAP_BUCKETS];
    char **idtostoragemap_bucketbytes[IDTOSTORAGEMAP_BUCKETS];
} threadlocalstoragetype;

mutex *_tls_type_access = NULL;
int32_t _tls_type_count = 0;
threadlocalstoragetype *_tls_type = NULL;

void *threadlocalstorage_GetByType(int32_t type) {
    uint64_t tid = thread_GetOurThreadId();
    mutex_Lock(_tls_type_access);
    if (type < 0 || type >= _tls_type_count) {
        mutex_Release(_tls_type_access);
        return NULL;
    }
    int bucket = (int)(
        (uint64_t)tid % (uint64_t)IDTOSTORAGEMAP_BUCKETS
    );
    threadlocalstoragetype *tltype = &_tls_type[type];
    int k = 0;
    while (k < tltype->idtostoragemap_bucketitems[bucket]) {
        if (tltype->idtostoragemap_bucketidlist[bucket][k] == tid) {
            char *resultptr = (
                tltype->idtostoragemap_bucketbytes[bucket][k]
            );
            mutex_Release(_tls_type_access);
            return resultptr;
        }
        k++;
    }
    // Ok, not found in bucket. Need to add new entry to bucket!
    // 1. Add space in the actual data pointer list of the bucket:
    int current_id_count = tltype->idtostoragemap_bucketitems[bucket];
    char **new_bytes = realloc(
        tltype->idtostoragemap_bucketbytes[bucket],
        sizeof(*tltype->idtostoragemap_bucketbytes[bucket]) *
            (current_id_count + 1)
    );
    if (!new_bytes) {
        mutex_Release(_tls_type_access);
        return NULL;
    }
    tltype->idtostoragemap_bucketbytes[bucket] = new_bytes;
    // 2. Add space in the thread id list of the bucket:
    uint64_t *new_idtostoragemap_bucketidlist = realloc(
        tltype->idtostoragemap_bucketidlist[bucket],
        sizeof(*tltype->idtostoragemap_bucketidlist[bucket]) *
            (current_id_count + 1)
    );
    if (!new_idtostoragemap_bucketidlist) {
        mutex_Release(_tls_type_access);
        return NULL;
    }
    tltype->idtostoragemap_bucketidlist[bucket] = (
        new_idtostoragemap_bucketidlist
    );
    // 3. Allocate data pointer for this bucket:
    tltype->idtostoragemap_bucketbytes[bucket][current_id_count] = (
        malloc(tltype->bytes)
    );
    if (!tltype->idtostoragemap_bucketbytes[bucket][current_id_count]) {
        mutex_Release(_tls_type_access);
        return NULL;
    }

    // Ok, everything allocated. Clear out new data for this thread,
    // remember that we added a new thread's data, and return:
    char *resultdata = (
        tltype->idtostoragemap_bucketbytes[bucket][current_id_count]
    );
    memset(
        resultdata, 0, tltype->bytes
    );
    tltype->idtostoragemap_bucketidlist[bucket][current_id_count] = tid;
    tltype->idtostoragemap_bucketitems[bucket]++;
    mutex_Release(_tls_type_access);
    return resultdata;
}

int threadlocalstorage_CleanTypeForThread(
        uint64_t tid, int32_t type
        ) {
    mutex_Lock(_tls_type_access);
    if (type < 0 || type >= _tls_type_count) {
        mutex_Release(_tls_type_access);
        return 1;
    }
    int bucket = (int)(
        (uint64_t)tid % (uint64_t)IDTOSTORAGEMAP_BUCKETS
    );
    // Call the clean function on all this thread's data:
    threadlocalstoragetype *tltype = &_tls_type[type];
    int k = 0;
    while (k < tltype->idtostoragemap_bucketitems[bucket]) {
        if (tltype->idtostoragemap_bucketidlist[bucket][k] == tid) {
            char *dataptr = (
                tltype->idtostoragemap_bucketbytes[bucket][k]
            );
            if (!dataptr) {
                k++;
                continue;
            }
            void (*clearHandler)(
                uint32_t storage_type_id, void *storageptr,
                uint64_t storage_bytes
            ) = tltype->clearHandler;
            tltype->idtostoragemap_bucketbytes[bucket][k] = (
                NULL
            );
            uint64_t bytesamount = tltype->bytes;
            mutex_Release(_tls_type_access);
            clearHandler(type, dataptr, bytesamount);
            free(dataptr);
            return 1;
        }
        k++;
    }
    mutex_Release(_tls_type_access);
    return 0;
}

int threadlocalstorage_UnallocateTypeForThread(
        uint64_t tid, int32_t type
        ) {
    mutex_Lock(_tls_type_access);
    if (type < 0 || type >= _tls_type_count) {
        mutex_Release(_tls_type_access);
        return 1;
    }
    int bucket = (int)(
        (uint64_t)tid % (uint64_t)IDTOSTORAGEMAP_BUCKETS
    );
    // Call the clean function on all this thread's data:
    threadlocalstoragetype *tltype = &_tls_type[type];
    int k = 0;
    while (k < tltype->idtostoragemap_bucketitems[bucket]) {
        if (tltype->idtostoragemap_bucketidlist[bucket][k] == tid) {
            if (tltype->idtostoragemap_bucketbytes[bucket][k]) {
                mutex_Release(_tls_type_access);
                return 0;
            }
            if (k + 1 < tltype->idtostoragemap_bucketitems[bucket]) {
                memmove(
                    &tltype->idtostoragemap_bucketbytes[bucket][k],
                    &tltype->idtostoragemap_bucketbytes[bucket][k + 1],
                    sizeof(tltype->idtostoragemap_bucketbytes[bucket][k])
                );
                memmove(
                    &tltype->idtostoragemap_bucketidlist[bucket][k],
                    &tltype->idtostoragemap_bucketidlist[bucket][k + 1],
                    sizeof(tltype->idtostoragemap_bucketidlist[bucket][k])
                );
            }
            tltype->idtostoragemap_bucketitems[bucket]--;
            continue;
        }
        k++;
    }
    mutex_Release(_tls_type_access);
    return 1;
}

void threadlocalstorage_CleanForThread(uint64_t tid) {
    // Clean out all the registered local storage we can find:
    int cleaned_something = 1;
    while (cleaned_something) {
        cleaned_something = 0;
        mutex_Lock(_tls_type_access);
        uint32_t type_id = 0;
        while (type_id < _tls_type_count) {
            mutex_Release(_tls_type_access);
            cleaned_something = (
                cleaned_something ||
                threadlocalstorage_CleanTypeForThread(tid, type_id)
            );
            mutex_Lock(_tls_type_access);
            type_id++;
        }
        mutex_Release(_tls_type_access);
    }
    // Now, actually remove the thread from the buckets:
    // (The thread should no longer be running any code right now,
    // so it should be impossible for any storage to have popped
    // back up in parallel after the above cleaning ended)
    mutex_Lock(_tls_type_access);
    uint32_t type_id = 0;
    while (type_id < _tls_type_count) {
        mutex_Release(_tls_type_access);
        int unallocate_result = (
            threadlocalstorage_UnallocateTypeForThread(tid, type_id)
        );
        assert(unallocate_result != 0);  // that should be impossible
        mutex_Lock(_tls_type_access);
        type_id++;
    }
    mutex_Release(_tls_type_access);
}


#if defined(__APPLE__) || defined(__OSX__)
uint64_t _last_sem_id = 0;
mutex *synchronize_sem_id = NULL;
uint64_t uniquesemidpart = 0;
static int _macos_semaphore_initdone = 0;
__attribute__((constructor)) void __init_semId_synchronize() {
    if (_macos_semaphore_initdone)
        return;
    _macos_semaphore_initdone = 1;
    synchronize_sem_id = mutex_Create();
    if (!synchronize_sem_id) {
        fprintf(stderr, "threading.c: error: failed to "
            "create semaphore naming synchronizer, fatal\n");
        _exit(1);
    }
    secrandom_GetBytes(
        (char*)&uniquesemidpart, sizeof(uniquesemidpart)
    );
}
#endif


semaphore *semaphore_Create(int value) {
    semaphore *s = malloc(sizeof(*s));
    if (!s) {
        return NULL;
    }
    memset(s, 0, sizeof(*s));
#ifdef ISWIN
    s->s = CreateSemaphore(NULL, value, INT_MAX, NULL);
    if (!s->s) {
        free(s);
        return NULL;
    }
#elif defined(__APPLE__) || defined(__OSX__)
    if (!_macos_semaphore_initdone)
        __init_semId_synchronize();
    mutex_Lock(synchronize_sem_id);
    _last_sem_id++;
    uint64_t semid = _last_sem_id;
    mutex_Release(synchronize_sem_id);
    char sem_name[128];
    snprintf(
        sem_name, sizeof(sem_name) - 1,
        "/h64semaphore%" PRIu64 "id%" PRIu64,
        uniquesemidpart, semid
    );
    s->sname = strdup(sem_name);
    if (!s->sname) {
        free(s);
        return NULL;
    }
    s->s = sem_open(sem_name, O_CREAT, 0600, value);
    if (s->s == SEM_FAILED) {
        free(s->sname);
        free(s);
        return NULL;
    }
#else
    if (sem_init(&s->s, 0, value) != 0) {
        free(s);
        return NULL;
    }
#endif
    return s;
}


void semaphore_Wait(semaphore *s) {
#ifdef ISWIN
    WaitForSingleObject(s->s, INFINITE);
#else
#if defined(__APPLE__) || defined(__OSX__)
    sem_wait(s->s);
#else
    sem_wait(&s->s);
#endif
#endif
}


void semaphore_Post(semaphore *s) {
#ifdef ISWIN
    ReleaseSemaphore(s->s, 1, NULL);
#else
#if defined(__APPLE__) || defined(__OSX__)
    sem_post(s->s);
#else
    sem_post(&s->s);
#endif
#endif
}


void semaphore_Destroy(semaphore *s) {
    if (!s) {
        return;
    }
#ifdef ISWIN
    CloseHandle(s->s);
#else
#if defined(__APPLE__) || defined(__OSX__)
    sem_unlink(s->sname);
    free(s->sname);
#else
    sem_destroy(&s->s);
#endif
#endif
    free(s);
}


mutex *mutex_Create() {
    mutex *m = malloc(sizeof(*m));
    if (!m) {
        return NULL;
    }
    memset(m, 0, sizeof(*m));
#ifdef ISWIN
    m->m = CreateMutex(NULL, FALSE, NULL);
    if (!m->m) {
        free(m);
        return NULL;
    }
#else
    pthread_mutexattr_t blub;
    pthread_mutexattr_init(&blub);
#ifndef NDEBUG
    pthread_mutexattr_settype(&blub, PTHREAD_MUTEX_ERRORCHECK);
#else
    pthread_mutexattr_settype(&blub, PTHREAD_MUTEX_NORMAL);
#endif
    while (pthread_mutex_init(&m->m, &blub) != 0) {
        if (errno != EAGAIN) {
            free(m);
            return NULL;
        }
    }
    pthread_mutexattr_destroy(&blub);
#endif
    return m;
}


void mutex_Destroy(mutex *m) {
    if (!m) {
        return;
    }
#ifdef ISWIN
    CloseHandle(m->m);
#else
    while (pthread_mutex_destroy(&m->m) != 0) {
        if (errno != EBUSY) {
            break;
        }
    }
#endif
    free(m);
}


void mutex_Lock(mutex *m) {
#ifdef ISWIN
    WaitForSingleObject(m->m, INFINITE);
#else
    ATTR_UNUSED int result = -1;
    while (result != 0) {
        result = pthread_mutex_lock(&m->m);
    }
#endif
}

int mutex_TryLock(mutex *m) {
#ifdef ISWIN
    if (WaitForSingleObject(m->m, 0) == WAIT_OBJECT_0) {
        return 1;
    }
    return 0;
#else
    if (pthread_mutex_trylock(&m->m) != 0) {
        return 0;
    }
    return 1;
#endif
}


void mutex_Release(mutex *m) {
#ifdef ISWIN
    ReleaseMutex(m->m);
#else
#ifndef NDEBUG
    //int i = pthread_mutex_unlock(&m->m);
    //printf("return value: %d\n", i);
    //assert(i == 0);
    assert(pthread_mutex_unlock(&m->m) == 0);
#else
    pthread_mutex_unlock(&m->m);
#endif
#endif
}


void thread_Detach(thread *t) {
#ifdef ISWIN
    CloseHandle(t->t);
#else
    pthread_detach(t->t);
#endif
    free(t);
}


struct spawninfo {
    void (*func)(void* userdata);
    thread *tptr;
    void* userdata;
};


#ifdef ISWIN
static unsigned __stdcall spawnthread(void *data) {
#else
static void* spawnthread(void *data) {
#endif
    struct spawninfo *sinfo = data;
    void *udata = sinfo->userdata;
    uint64_t tid = thread_GetOurThreadId();
    sinfo->tptr->id = tid;
    semaphore_Post(sinfo->tptr->_setid_semaphore);
    void (*cbfunc)(void *) = sinfo->func;
    free(sinfo);
    cbfunc(udata);
    threadlocalstorage_CleanForThread(tid);
#ifdef ISWIN
    return 0;
#else
    return NULL;
#endif
}


thread *thread_Spawn(
        void (*func)(void *userdata),
        void *userdata
        ) {
    return thread_SpawnWithPriority(
        THREAD_PRIO_NORMAL, func, userdata
    );
}

uint64_t thread_GetId(thread *t) {
    return t->id;
}

thread *thread_SpawnWithPriority(
        int priority,
        void (*func)(void* userdata), void *userdata
        ) {
    // Make sure if we never spawned a thread bfore that
    // some things are set up:
    if (!_markedmainthread)
        thread_MarkAsMainThread();
    if (!_tls_type_access) {
        _tls_type_access = mutex_Create();
        if (!_tls_type_access)
            return NULL;
    }

    // Ok, allocate info for spawning:
    struct spawninfo* sinfo = malloc(sizeof(*sinfo));
    if (!sinfo)
        return NULL;
    memset(sinfo, 0, sizeof(*sinfo));
    sinfo->func = func;
    sinfo->userdata = userdata;
    thread *t = malloc(sizeof(*t));
    if (!t) {
        free(sinfo);
        return NULL;
    }
    memset(t, 0, sizeof(*t));
    sinfo->tptr = t;
    t->_setid_semaphore = semaphore_Create(0);
    if (!t->_setid_semaphore) {
        free(t);
        free(sinfo);
        return NULL;
    }
#ifdef ISWIN
    HANDLE h = (HANDLE)_beginthreadex(NULL, 0, spawnthread, sinfo, 0, NULL);
    if (h == INVALID_HANDLE) {
        semaphore_Destroy(t->_setid_semaphore);
        free(t);
        free(sinfo);
        return NULL;
    }
    t->t = h;
#else
    while (pthread_create(&t->t, NULL, spawnthread, sinfo) != 0) {
        if (errno != EAGAIN) {
            semaphore_Destroy(t->_setid_semaphore);
            free(t);
            free(sinfo);
            return NULL;
        }
    }
    #if defined(__linux__) || defined(__LINUX__)
    if (priority == 0) {
        struct sched_param param;
        memset(&param, 0, sizeof(param));
        param.sched_priority = sched_get_priority_min(SCHED_BATCH);
        pthread_setschedparam(t->t, SCHED_BATCH, &param);
    }
    if (priority == 2) {
        struct sched_param param;
        int policy = 0;
        pthread_getschedparam(t->t, &policy, &param);
        param.sched_priority = sched_get_priority_max(policy);
        pthread_setschedparam(t->t, policy, &param);
    }
    #endif
#endif
    semaphore_Wait(t->_setid_semaphore);
    semaphore_Destroy(t->_setid_semaphore);
    t->_setid_semaphore = NULL;
    return t;
}


int thread_InMainThread() {
    if (!_markedmainthread)
        thread_MarkAsMainThread();
#ifndef ISWIN
    return (pthread_self() == mainThread);
#else
    return (GetCurrentThreadId() == mainThread);
#endif
}

uint64_t thread_GetOurThreadId() {
    #if defined(__LINUX__) || defined(__linux__)
    return syscall(__NR_gettid);
    #elif defined(_WIN32) || defined(_WIN64)
    return GetCurrentThreadId();
    #elif defined(__APPLE__) || defined(__MACOS__)
    uint64_t tid;
    pthread_threadid_np(NULL, &tid);
    return tid;
    #elif defined(__FREEBSD__) || defined(__FreeBSD__)
    return pthread_getthreadid_np();
    #else
    #error "unsupported platform"
    #endif
}

void thread_Join(thread *t) {
#ifdef ISWIN
    WaitForMultipleObjects(1, &t->t, TRUE, INFINITE);
#else
    pthread_join(t->t, NULL);
#endif
    free(t);
}


typedef struct threadevent {
    h64socket *_targetside, *_sourceside;
    volatile int set;
    mutex *datalock;
} threadevent;

void threadevent_Free(threadevent *te) {
    if (!te)
        return;
    sockets_Destroy(te->_targetside);
    sockets_Destroy(te->_sourceside);
    mutex_Destroy(te->datalock);
    free(te);
}

threadevent *threadevent_Create() {
    threadevent *te = malloc(sizeof(*te));
    if (!te) {
        #if !defined(NDEBUG) && defined(DEBUG_SOCKETPAIR)
        h64fprintf(stderr,
            "horsevm: warning: malloc() failure "
            "in threadevent_Create() of 'te' struct, "
            "returning NULL\n"
        );
        #endif
        return NULL;
    }
    memset(te, 0, sizeof(*te));
    if (!sockets_NewPair(&te->_sourceside, &te->_targetside)) {
        #if !defined(NDEBUG) && defined(DEBUG_SOCKETPAIR)
        h64fprintf(stderr,
            "horsevm: warning: sockets_NewPair() failure "
            "in threadevent_Create(), returning NULL\n"
        );
        #endif
        threadevent_Free(te);
        return NULL;
    }
    te->datalock = mutex_Create();
    if (!te->datalock) {
        #if !defined(NDEBUG) && defined(DEBUG_SOCKETPAIR)
        h64fprintf(stderr,
            "horsevm: warning: mutex creation failure "
            "in threadevent_Create(), returning NULL\n"
        );
        #endif
        threadevent_Free(te);
        return NULL;
    }
    return te;
}

int mutex_IsLocked(mutex *m) {
    int result = mutex_TryLock(m);
    if (result)
        mutex_Release(m);
    return (result == 0);
}

int32_t threadlocalstorage_RegisterType(
        uint64_t bytes, void (*clearHandler)(
            uint32_t storage_type_id, void *storageptr,
            uint64_t storage_bytes
        )) {
    if (!_tls_type_access) {
        _tls_type_access = mutex_Create();
        if (!_tls_type_access)
            return -1;
    }
    mutex_Lock(_tls_type_access);
    threadlocalstoragetype *new_types = realloc(
        _tls_type, sizeof(*_tls_type) * (_tls_type_count + 1)
    );
    if (!new_types) {
        mutex_Release(_tls_type_access);
        return -1;
    }
    _tls_type = new_types;
    memset(&_tls_type[_tls_type_count], 0,
           sizeof(_tls_type[_tls_type_count]));
    _tls_type[_tls_type_count].bytes = bytes;
    _tls_type[_tls_type_count].clearHandler = clearHandler;
    int64_t id = _tls_type_count;
    _tls_type_count++;
    mutex_Release(_tls_type_access);
    return id;
}

int threadevent_PollIsSet(threadevent *te, int unsetifset) {
    mutex_Lock(te->datalock);
    if (te->set) {
        if (unsetifset)
            te->set = 0;
        mutex_Release(te->datalock);
        return 1;
    }
    mutex_Release(te->datalock);
    return 0;
}

h64socket *threadevent_WaitForSocket(threadevent *te) {
    return te->_targetside;
}


void threadevent_FlushWakeUpEvents(threadevent *te) {
    int fd = te->_targetside->fd;
    char c;
    ssize_t recvbytes = 1;
    while (recvbytes > 0) {
        recvbytes = recv(
            fd, &c, 1, 0
        );
    }
}

void threadevent_Unset(threadevent *te) {
    assert(te != NULL);
    mutex_Lock(te->datalock);
    te->set = 0;
    threadevent_FlushWakeUpEvents(te);
    mutex_Release(te->datalock);
}


void threadevent_Set(threadevent *te) {
    mutex_Lock(te->datalock);
    te->set = 1;
    mutex_Release(te->datalock);
    char c = 0;
    while (1) {
        ssize_t result = send(
            te->_sourceside->fd, &c, 1, 0
        );
        if (result < 0) {
            #if defined(_WIN32) || defined(_WIN64)
            uint32_t errc = GetLastError();
            if (errc != WSA_IO_INCOMPLETE &&
                    errc != WSA_IO_PENDING &&
                    errc != WSAEINTR &&
                    errc != WSAEWOULDBLOCK &&
                    errc != WSAEINPROGRESS &&
                    errc != WSAEALREADY) {
            #else
            if (errno != EAGAIN && errno != EWOULDBLOCK &&
                    errno != EPIPE) {
            #endif
                break;
            }
            h64sockset writeset = {0};
            sockset_Init(&writeset);
            if (!sockset_Add(
                    &writeset, te->_sourceside->fd,
                    H64SOCKSET_WAITWRITE | H64SOCKSET_WAITERROR
                    )) {
                // out of memory??? wait tiny bit and retry:
                sockset_Uninit(&writeset);
                datetime_Sleep(10);
                continue;
            }
            sockset_Wait(&writeset, 0);
            sockset_Uninit(&writeset);
            continue;
        } else {
            break;
        }
    }
}

int threadevent_WaitUntilSet(
        threadevent *te, uint64_t timeout_ms,
        int unsetifset
        ) {
    int fd = te->_targetside->fd;
    mutex_Lock(te->datalock);
    if (te->set) {
        if (unsetifset)
            te->set = 0;
        mutex_Release(te->datalock);
        return 1;
    }
    mutex_Release(te->datalock);

    int64_t timeremain_ms = 0;
    if (timeout_ms > 0 && timeout_ms > (uint64_t)INT64_MAX)
        timeremain_ms = INT64_MAX;
    else if (timeout_ms > 0)
        timeremain_ms = timeout_ms;
    int64_t origtimeremain_ms = timeremain_ms;
    uint64_t start = datetime_Ticks();
    while (1) {
        h64sockset readset = {0};
        sockset_Init(&readset);
        if (!sockset_Add(
                &readset, fd,
                H64SOCKSET_WAITREAD | H64SOCKSET_WAITERROR
                )) {
            // out of memory??? wait tiny bit and retry:
            sockset_Uninit(&readset);
            datetime_Sleep(10);
            continue;
        }
        int result = sockset_Wait(&readset, timeremain_ms);
        sockset_Uninit(&readset);
        if (result > 0) {
            char c;
            ssize_t recvbytes = 1;
            while (recvbytes > 0) {
                recvbytes = recv(
                    fd, &c, 1, 0
                );
            }
        }
        mutex_Lock(te->datalock);
        if (te->set) {
            if (unsetifset)
                te->set = 0;
            mutex_Release(te->datalock);
            return 1;
        }
        mutex_Release(te->datalock);
        if (timeremain_ms > 0) {
            timeremain_ms = (
                origtimeremain_ms - (datetime_Ticks() - start)
            );
            if (timeremain_ms >= 1) {
                continue;
            }
            return 0;
        } else {
            return 0;
        }
    }
}