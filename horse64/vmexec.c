
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "poolalloc.h"
#include "gcvalue.h"
#include "stack.h"
#include "vmexec.h"

h64vmthread *vmthread_New() {
    h64vmthread *vmthread = malloc(sizeof(*vmthread));
    if (!vmthread)
        return NULL;
    memset(vmthread, 0, sizeof(*vmthread));

    vmthread->heap = poolalloc_New(sizeof(h64gcvalue));
    if (!vmthread->heap) {
        vmthread_Free(vmthread);
        return NULL;
    }
    return vmthread;
}

void vmthread_Free(h64vmthread *vmthread) {
    if (vmthread->heap) {
        // Free items on heap, FIXME

        // Free heap:
        poolalloc_Destroy(vmthread->heap);
    }
}

void vmthread_WipeFuncStack(h64vmthread *vmthread) {
    assert(VMTHREAD_FUNCSTACKBOTTOM(vmthread) <=
           STACK_SIZE(vmthread->stack));
    if (VMTHREAD_FUNCSTACKBOTTOM(vmthread) < STACK_SIZE(vmthread->stack)
            ) {
        int result = stack_ToSize(
            vmthread->stack,
            VMTHREAD_FUNCSTACKBOTTOM(vmthread),
            0
        );
        assert(result != 0);  // shrink should always succeed.
        return;
    }
}
