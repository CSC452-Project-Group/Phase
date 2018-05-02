#include <usloss.h>
#include <usyscall.h>
#include <assert.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <phase4.h>
#include <phase5.h>
#include <libuser.h>
#include <providedPrototypes.h>
#include <vm.h>
#include <string.h>

extern void mbox_create(USLOSS_Sysargs *args_ptr);
extern void mbox_release(USLOSS_Sysargs *args_ptr);
extern void mbox_send(USLOSS_Sysargs *args_ptr);
extern void mbox_receive(USLOSS_Sysargs *args_ptr);
extern void mbox_condsend(USLOSS_Sysargs *args_ptr);
extern void mbox_condreceive(USLOSS_Sysargs *args_ptr);

VmStats  vmStats;
FTE *frameT = NULL;
int pagerT[MAXPAGERS];
int faultC;
int cHandler = 0;
int track = 0, sector = 0;
void *vmRegion = NULL;
int cMutex;
FaultMsg faults[MAXPROC]; /* Note that a process can have only
                           * one fault at a time, so we can
                           * allocate the messages statically
                           * and index them by pid. */


static int Pager(char *buf);
static void FaultHandler(int type, void * offset);
static void vmInit(USLOSS_Sysargs *USLOSS_SysargsPtr);
static void vmDestroy(USLOSS_Sysargs *USLOSS_SysargsPtr);

void printPageTable(int pid);
void setUserMode();
void *vmInitReal(int, int, int, int);
void vmDestroyReal();

#define CHECKMODE {                     \
    if ( !(USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE)) {                \
        USLOSS_Console("Trying to invoke syscall from kernel\n");   \
        USLOSS_Halt(1);                     \
    }                           \
}

/*
 *----------------------------------------------------------------------
 *
 * start4 --
 *
 * Initializes the VM system call handlers. 
 *
 * Results:
 *      MMU return status
 *
 * Side effects:
 *      The MMU is initialized.
 *
 *----------------------------------------------------------------------
 */
int
start4(char *arg)
{
    int pid;
    int result;
    int status;

    /* to get user-process access to mailbox functions */
    systemCallVec[SYS_MBOXCREATE]      = mbox_create;
    systemCallVec[SYS_MBOXRELEASE]     = mbox_release;
    systemCallVec[SYS_MBOXSEND]        = mbox_send;
    systemCallVec[SYS_MBOXRECEIVE]     = mbox_receive;
    systemCallVec[SYS_MBOXCONDSEND]    = mbox_condsend;
    systemCallVec[SYS_MBOXCONDRECEIVE] = mbox_condreceive;

    /* user-process access to VM functions */
    systemCallVec[SYS_VMINIT]    = vmInit;
    systemCallVec[SYS_VMDESTROY] = vmDestroy; 

    // Initialize other structures as needed
    for(int i =0; i < MAXPAGERS; i++)
        pagerT[i] = -1;

    result = Spawn("Start5", start5, NULL, 8*USLOSS_MIN_STACK, 2, &pid);
    if (result != 0) {
        USLOSS_Console("start4(): Error spawning start5\n");
        Terminate(1);
    }
    result = Wait(&pid, &status);
    if (result != 0) {
        USLOSS_Console("start4(): Error waiting for start5\n");
        Terminate(1);
    }

    Terminate(0);
    return 0; // not reached

} /* start4 */

/*
 *----------------------------------------------------------------------
 *
 * VmInit --
 *
 * Stub for the VmInit system call.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      VM system is initialized.
 *
 *----------------------------------------------------------------------
 */
static void
vmInit(USLOSS_Sysargs *USLOSS_SysargsPtr)
{
    CHECKMODE;

    USLOSS_SysargsPtr->arg1 = vmInitReal((long)USLOSS_SysargsPtr->arg1, (long)USLOSS_SysargsPtr->arg2, \
                            (long)USLOSS_SysargsPtr->arg3, (long)USLOSS_SysargsPtr->arg4);

    USLOSS_SysargsPtr->arg4 = USLOSS_SysargsPtr->arg1 < 0 ? USLOSS_SysargsPtr->arg1 : (void *)(long)0;
    setUserMode();

} /* vmInit */

/*
 *----------------------------------------------------------------------
 *
 * vmDestroy --
 *
 * Stub for the VmDestroy system call.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      VM system is cleaned up.
 *
 *----------------------------------------------------------------------
 */
static void
vmDestroy(USLOSS_Sysargs *USLOSS_SysargsPtr)
{
    CHECKMODE;

    vmDestroyReal();
    setUserMode();
} /* vmDestroy */

/*
 *----------------------------------------------------------------------
 *
 * vmInitReal --
 *
 * Called by vmInit.
 * Initializes the VM system by configuring the MMU and setting
 * up the page tables.
 *
 * Results:
 *      Address of the VM region.
 *
 * Side effects:
 *      The MMU is initialized.
 *
 *----------------------------------------------------------------------
 */
void *
vmInitReal(int mappings, int pages, int frames, int pagers)
{
    int status;
    int dummy;

    CHECKMODE;

    // the number of mappings should equal the number of virtual pages
    if(mappings != pages)
        return (void*)(long)-1;

    // installing a handler for the MMU_Init interrupt
    status = USLOSS_MmuInit(mappings, pages, frames, USLOSS_MMU_MODE_TLB);
    if (status != USLOSS_MMU_OK) {
        USLOSS_Console("vmInitReal: couldn't initialize MMU, status %d\n", status);
        abort();
    }
    USLOSS_IntVec[USLOSS_MMU_INT] = FaultHandler;

    /*
     * Initialize page table.
     */
    for (int i = 0; i < MAXPROC; i++){
        processes[i].numPages = pages;
        processes[i].pageTable = NULL;
    }

    /*
     * Initialize fault Msg.
     */
    for (int i = 0; i < MAXPROC; i++){
        faults[i].pid = -1;
        faults[i].addr = NULL;
        faults[i].replyMbox = MboxCreate(1,0);
    }

    /*
     * Initialize frame table.
     */
    frameT = (FTE*)malloc(frames * sizeof(FTE));
    for (int i = 0; i < frames; i++){
        frameT[i].pid = -1;
        frameT[i].state = UNUSED;
        frameT[i].page = -1;
    }

    /* 
     * Create the fault mailbox or semaphore
     */
    faultC = MboxCreate(pagers, sizeof(FaultMsg));
    cMutex = semcreateReal(1);
    /*
     * Fork the pagers.
     */
    for (int i = 0; i < pagers; i++){
        char arg[5];
        sprintf(arg, "%d", i);
        pagerT[i] = fork1("Pager", Pager, arg, 8 * USLOSS_MIN_STACK, PAGER_PRIORITY);
    }

    /*
     * Set up dick block
     */
    int diskBlocks, sectorSize, trackSize;
    diskSizeReal(1, &sectorSize, &trackSize, &diskBlocks);
    diskBlocks *= 2;

    /*
     * Zero out, then initialize, the vmStats structure
     */
    memset((char *) &vmStats, 0, sizeof(VmStats));
    vmStats.pages = pages;
    vmStats.frames = frames;
    vmStats.freeFrames = frames;
    vmStats.diskBlocks = diskBlocks;
    vmStats.freeDiskBlocks = diskBlocks;
    vmStats.switches = 0;
    vmStats.faults = 0;
    vmStats.new = 0;
    vmStats.pageIns = 0;
    vmStats.pageOuts = 0;
    vmStats.replaced = 0;

    return USLOSS_MmuRegion(&dummy);
} /* vmInitReal */

/*
 *----------------------------------------------------------------------
 *
 * PrintStats --
 *
 *      Print out VM statistics.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Stuff is printed to the USLOSS_Console.
 *
 *----------------------------------------------------------------------
 */
void
PrintStats(void)
{
    USLOSS_Console("VmStats\n");
    USLOSS_Console("pages:          %d\n", vmStats.pages);
    USLOSS_Console("frames:         %d\n", vmStats.frames);
    USLOSS_Console("diskBlocks:     %d\n", vmStats.diskBlocks);
    USLOSS_Console("freeFrames:     %d\n", vmStats.freeFrames);
    USLOSS_Console("freeDiskBlocks: %d\n", vmStats.freeDiskBlocks);
    USLOSS_Console("switches:       %d\n", vmStats.switches);
    USLOSS_Console("faults:         %d\n", vmStats.faults);
    USLOSS_Console("new:            %d\n", vmStats.new);
    USLOSS_Console("pageIns:        %d\n", vmStats.pageIns);
    USLOSS_Console("pageOuts:       %d\n", vmStats.pageOuts);
    USLOSS_Console("replaced:       %d\n", vmStats.replaced);
} /* PrintStats */

/*
 *----------------------------------------------------------------------
 *
 * vmDestroyReal --
 *
 * Called by vmDestroy.
 * Frees all of the global data structures
 *
 * Results:
 *      None
 *
 * Side effects:
 *      The MMU is turned off.
 *
 *----------------------------------------------------------------------
 */
void
vmDestroyReal(void)
{
    vmRegion = NULL;

    int errorCode;
    CHECKMODE;
    errorCode = USLOSS_MmuDone();
    if (errorCode != USLOSS_MMU_OK){
        USLOSS_Console("ERROR: Turn off MMU failed! Exiting....\n");
        USLOSS_Halt(1);        
    }

    /*
     * Kill the pagers here.
     */
    int status;
    for(int i = 0; i < MAXPAGERS; i++){
        if (pagerT[i] != -1){
            MboxSend(faultC, NULL, 0);
            zap(pagerT[i]);
            join(&status);
        }
    }

    // release the faults message and falut mailbox.
    for (int i = 0; i < MAXPROC; i++) {
        MboxRelease(faults[i].replyMbox);
    }
    MboxRelease(faultC);

    /* 
     * Print vm statistics.
     */
    PrintStats();
} /* vmDestroyReal */

/*
 *----------------------------------------------------------------------
 *
 * FaultHandler --
 *
 * Handles an MMU interrupt. Simply stores information about the
 * fault in a queue, wakes a waiting pager, and blocks until
 * the fault has been handled.
 *
 * Results:
 * None.
 *
 * Side effects:
 * The current process is blocked until the fault is handled.
 *
 *----------------------------------------------------------------------
 */
static void
FaultHandler(int type /* MMU_INT */,
             void * offset  /* Offset within VM region */)
{
    int cause;
    assert(type == USLOSS_MMU_INT);
    cause = USLOSS_MmuGetCause();
    assert(cause == USLOSS_MMU_FAULT);
    vmStats.faults++;

    /*
     * Fill in faults[pid % MAXPROC], send it to the pagers, and wait for the
     * reply.
     */
    int pid = getpid();
    faults[pid % MAXPROC].pid = pid;
    faults[pid % MAXPROC].addr = (void*)((long)(processes[pid % MAXPROC].pageTable) + (long)offset);
    // send fault mailbox to pagers and block the current process
    MboxSend(faultC, &pid, sizeof(int));
    MboxReceive(faults[pid % MAXPROC].replyMbox, NULL, 0);
} /* FaultHandler */

/*
 *----------------------------------------------------------------------
 *
 * Pager --
 *
 * Kernel process that handles page faults and does page replacement.
 *
 * Results:
 * None.
 *
 * Side effects:
 * None.
 *
 *----------------------------------------------------------------------
 */
static int
Pager(char *buf)
{
    FaultMsg *fault = NULL;
    Process *prc = NULL;
    PTE* newPage = NULL;
    PTE* oldPage = NULL;
    int new_pageNum = 0, old_pageNum, frame = 0, pid = 0, result = -1, access = -1;
    char buffer[USLOSS_MmuPageSize()];

    while(1) {
        //get segment fault process pid
        MboxReceive(faultC, &pid, sizeof(int));
        //based on the pid, get fault message
        fault = &faults[pid%MAXPROC];
        if(isZapped())
            break;
        prc = &processes[pid%MAXPROC];
        //calculate new page number
        new_pageNum = ((long)fault->addr - (long)prc->pageTable) / USLOSS_MmuPageSize();
        //based on new page number, get new page PTE
        newPage =  &(prc->pageTable[new_pageNum]);
        
        sempReal(cMutex);
        if (vmStats.freeFrames > 0){
            for(int i = 0; i < vmStats.frames; i++){
                if (frameT[i].state == UNUSED){
                    frame = i;
                    break;
                }
            }
        }
        else{
            frame = -1;
            while(frame == -1){
                result = USLOSS_MmuGetAccess(cHandler, &access);
                if ((access & USLOSS_MMU_REF) == 0)
                    frame = cHandler;
                else
                    result = USLOSS_MmuSetAccess(cHandler, access & 0x2);
                cHandler = (cHandler + 1) % vmStats.frames;
            }
        }
        semvReal(cMutex);

        result = USLOSS_MmuGetAccess(frame, &access);
        result = USLOSS_MmuSetAccess(frame, access & 0x2);

        if (frameT[frame].state == UNUSED)
            vmStats.freeFrames--;
        else{
            result = USLOSS_MmuGetAccess(frame, &access);
            //update old page
            old_pageNum = frameT[frame].page;
            oldPage = &processes[frameT[frame].pid].pageTable[old_pageNum];
            oldPage->frame = -1;
            oldPage->state = INCORE;
            //map frame to page 0
            result = USLOSS_MmuMap(TAG, 0, frame, USLOSS_MMU_PROT_RW);
            if (result != USLOSS_MMU_OK){
                USLOSS_Console("Pager():\t mmu map failed1 in %d, %d, %d\n", 0, frame, result);
                USLOSS_Halt(1);                       
            }
            memcpy(&buffer, vmRegion, USLOSS_MmuPageSize());
            //if (strlen(buffer) != 0){
            if(access >= 2){
                result = USLOSS_MmuUnmap(frame, old_pageNum);
                //USLOSS_Console("\nPager():\t %d, %d, %d, %s\n\n", old_pageNum, new_pageNum, frame, buffer);
                if (oldPage->diskBlock == UNUSED){
                    //USLOSS_Console("\nchild(%d) use disk block %d\n\n", pid, frameT[frame].pid);
                    oldPage->diskBlock++;
                    oldPage->track = track / 2;
                    oldPage->sector = sector % 2 == 0 ? 0 : USLOSS_MmuPageSize()/USLOSS_DISK_SECTOR_SIZE;
                    track++;
                    sector++;
                }
                diskWriteReal (SWAPDISK, oldPage->track, oldPage->sector, USLOSS_MmuPageSize()/USLOSS_DISK_SECTOR_SIZE, &buffer);
                vmStats.pageOuts++;
            }
            result = USLOSS_MmuUnmap(TAG, 0);
        }
        
        if(newPage->state == UNUSED){
            result = USLOSS_MmuMap(TAG, 0, frame, USLOSS_MMU_PROT_RW);
            if (result != USLOSS_MMU_OK){
                USLOSS_Console("Pager():\t mmu map failed2 in %d, %d, %d\n", 0, frame, result);
                USLOSS_Halt(1);                       
            }
            memset(vmRegion, '\0', USLOSS_MmuPageSize());
            vmStats.new++;
            result = USLOSS_MmuUnmap(TAG, 0);
            result = USLOSS_MmuSetAccess(frame, 1);
        }
        else if(newPage->diskBlock != UNUSED && newPage != oldPage){
            diskReadReal (SWAPDISK, newPage->track, newPage->sector, USLOSS_MmuPageSize()/USLOSS_DISK_SECTOR_SIZE, &buffer);
            result = USLOSS_MmuMap(TAG, 0, frame, USLOSS_MMU_PROT_RW);
            if (result != USLOSS_MMU_OK){
                USLOSS_Console("Pager():\t mmu map failed3 in %d, %d, %d\n", 0, frame, result);
                USLOSS_Halt(1);                       
            }
            memcpy(vmRegion, &buffer, USLOSS_MmuPageSize());
            vmStats.pageIns++;
            result = USLOSS_MmuUnmap(TAG, 0);          
        }

        // update frame table
        frameT[frame].pid = pid;
        frameT[frame].state = INFRAME;
        frameT[frame].page = new_pageNum;

        // unpdate page table
        prc->pageTable[new_pageNum].state = INFRAME;
        prc->pageTable[new_pageNum].frame = frame;
        MboxSend(fault->replyMbox, NULL, 0);
    }
    return 0;
} /* Pager */


void
setUserMode(){
    int result = USLOSS_PsrSet( USLOSS_PsrGet() & ~USLOSS_PSR_CURRENT_MODE );
    if(result != USLOSS_DEV_OK) {
        USLOSS_Console("ERROR: USLOSS_PsrSet failed! Exiting....\n");
        USLOSS_Halt(1);
    }
} /* setUserMode */
