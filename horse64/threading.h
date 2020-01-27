#ifndef HORSE3D_THREADING_H_
#define HORSE3D_THREADING_H_


typedef struct mutex mutex;

typedef struct semaphore semaphore;

typedef struct threadinfo thread;


semaphore* semaphore_Create(int value);


void semaphore_Wait(semaphore* s);


void semaphore_Post(semaphore* s);


void semaphore_Destroy(semaphore* s) ;


mutex* mutex_Create();


void mutex_Lock(mutex* m);


int mutex_TryLock(mutex* m);


void mutex_Release(mutex* m);


void mutex_Destroy(mutex* m);


thread *thread_Spawn(
    void (*func)(void *userdata),
    void *userdata
);


#define THREAD_PRIO_LOW 1
#define THREAD_PRIO_NORMAL 2
#define THREAD_PRIO_HIGH 3


thread *thread_SpawnWithPriority(
    int priority,
    void (*func)(void* userdata), void *userdata
);


void thread_Detach(thread *t);


void thread_Join(thread *t);


int thread_InMainThread();

#endif  // HORSE3D_THREADING_H_
