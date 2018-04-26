/*
 *  File:  libuser.c
 *
 *  Description:  This file contains the interface declarations
 *                to the OS kernel support package.
 *
 */

#include <usloss.h>
#include <usyscall.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <phase4.h>
#include <phase5.h>
#include <libuser.h>

#define CHECKMODE {    \
    if (USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) { \
        USLOSS_Console("Trying to invoke syscall from kernel\n"); \
        USLOSS_Halt(1);  \
    }  \
}

// Phase 5
int Mbox_Create(int numslots, int slotsize, int *mboxID)
{
    USLOSS_Sysargs sysArg;

    CHECKMODE;
    sysArg.number = SYS_MBOXCREATE;
    sysArg.arg1 = (void *) (long) numslots;
    sysArg.arg2 = (void *) (long) slotsize;
    USLOSS_Syscall(&sysArg);
    *mboxID = (int) (long) sysArg.arg1;
    return (int) (long) sysArg.arg4;
} /* end of Mbox_Create */


int Mbox_Release(int mboxID)
{
    USLOSS_Sysargs sysArg;

    CHECKMODE;
    sysArg.number = SYS_MBOXRELEASE;
    sysArg.arg1 = (void *) (long) mboxID;
    USLOSS_Syscall(&sysArg);
    return (int) (long) sysArg.arg4;
} /* end of Mbox_Release */


int Mbox_Send(int mboxID, void *msgPtr, int msgSize)
{
    USLOSS_Sysargs sysArg;

    CHECKMODE;
    sysArg.number = SYS_MBOXSEND;
    sysArg.arg1 = (void *) (long) mboxID;
    sysArg.arg2 = (void *) (long) msgPtr;
    sysArg.arg3 = (void *) (long) msgSize;
    USLOSS_Syscall(&sysArg);
    return (int) (long) sysArg.arg4;
} /* end of Mbox_Send */


int Mbox_Receive(int mboxID, void *msgPtr, int msgSize)
{
    USLOSS_Sysargs sysArg;

    CHECKMODE;
    sysArg.number = SYS_MBOXRECEIVE;
    sysArg.arg1 = (void *) (long) mboxID;
    sysArg.arg2 = (void *) (long) msgPtr;
    sysArg.arg3 = (void *) (long) msgSize;
    USLOSS_Syscall( &sysArg );
        /*
         * This doesn't belong here. The copy should by done by the
         * system call.
         */
        if ( (int) (long) sysArg.arg4 == -1 )
                return (int) (long) sysArg.arg4;
        memcpy( (char*)msgPtr, (char*)sysArg.arg2, (int) (long) sysArg.arg3);
        return 0;

} /* end of Mbox_Receive */

int Mbox_CondSend(int mboxID, void *msgPtr, int msgSize)
{
    USLOSS_Sysargs sysArg;

    CHECKMODE;
    sysArg.number = SYS_MBOXCONDSEND;
    sysArg.arg1 = (void *) (long) mboxID;
    sysArg.arg2 = (void *) (long) msgPtr;
    sysArg.arg3 = (void *) (long) msgSize;
    USLOSS_Syscall(&sysArg);
    return ((int) (long) sysArg.arg4);
} /* end of Mbox_CondSend */


int Mbox_CondReceive(int mboxID, void *msgPtr, int msgSize)
{
    USLOSS_Sysargs sysArg;

    CHECKMODE;
    sysArg.number = SYS_MBOXCONDRECEIVE;
    sysArg.arg1 = (void *) (long) mboxID;
    sysArg.arg2 = (void *) (long) msgPtr;
    sysArg.arg3 = (void *) (long) msgSize;
    USLOSS_Syscall( &sysArg );
    return ((int) (long) sysArg.arg4);
} /* end of Mbox_CondReceive */

/*
 *  Routine:  VmInit
 *
 *  Description: Initializes the virtual memory system.
 *
 *  Arguments:    int mappings -- # of mappings in the MMU
 *                int pages -- # pages in the VM region
 *                int frames -- # physical page frames
 *                int pagers -- # pagers to use
 *
 *  Return Value: address of VM region, NULL if there was an error
 *
 */
int VmInit(int mappings, int pages, int frames, int pagers, void **region)
{
    USLOSS_Sysargs sysArg;
    int result;

    CHECKMODE;

    sysArg.number = SYS_VMINIT;
    sysArg.arg1 = (void *) (long) mappings;
    sysArg.arg2 = (void *) (long) pages;
    sysArg.arg3 = (void *) (long) frames;
    sysArg.arg4 = (void *) (long) pagers;

    USLOSS_Syscall(&sysArg);

    *region = sysArg.arg1;  // return address of VM Region

    result = (int) (long) sysArg.arg4;

    if (sysArg.arg4 == 0) {
        return 0;
    } else {
        return result;
    }
} /* VmInit */


int VmDestroy(void) {
    USLOSS_Sysargs     sysArg;

    CHECKMODE;
    sysArg.number = SYS_VMDESTROY;
    USLOSS_Syscall(&sysArg);
    return (int) (long) sysArg.arg1;
} /* VmDestroy */

/* end libuser.c */
