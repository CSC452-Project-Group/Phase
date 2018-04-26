/*
 * skeleton.c
 *
 * This is a skeleton for phase5 of the programming assignment. It
 * doesn't do much -- it is just intended to get you started.
 */


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

static int Pager(char *buf);
void printPageTable(int pid);

extern void mbox_create(USLOSS_Sysargs *args_ptr);
extern void mbox_release(USLOSS_Sysargs *args_ptr);
extern void mbox_send(USLOSS_Sysargs *args_ptr);
extern void mbox_receive(USLOSS_Sysargs *args_ptr);
extern void mbox_condsend(USLOSS_Sysargs *args_ptr);
extern void mbox_condreceive(USLOSS_Sysargs *args_ptr);

Process processes[MAXPROC];
int pagerPid[MAXPAGERS];
int numPages = 0, numFrames = 0;
int faultMBox;

FaultMsg faults[MAXPROC]; /* Note that a process can have only
                           * one fault at a time, so we can
                           * allocate the messages statically
                           * and index them by pid. */
VmStats  vmStats;
FTE *frameTable = NULL;

static void FaultHandler(int type, void * offset);

static void vmInit(USLOSS_Sysargs *USLOSS_SysargsPtr);
static void vmDestroy(USLOSS_Sysargs *USLOSS_SysargsPtr);
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
    sys_vec[SYS_VMINIT]    = vmInit;
    sys_vec[SYS_VMDESTROY] = vmDestroy; 

    // Initialize the phase 5 process table

    // Initialize other structures as needed

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
    CheckMode();

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
   CheckMode();
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

   CheckMode();

   if (mappings < 0 || pages < 0 || frames < 0 || pagers < 0)
	   return (void *)-1;
   if (mappings != pages)
	   return (void *)-1;
   if (pagers > MAXPAGERS)
	   return (void *)-1;	

   status = USLOSS_MmuInit(mappings, pages, frames, USLOSS_MMU_MODE_TLB);
   if (status != MMU_OK) {
      USLOSS_Console("vmInitReal: couldn't initialize MMU, status %d\n", status);
      abort();
   }
   USLOSS_IntVec[USLOSS_MMU_INT] = FaultHandler;

   /*
    * Initialize page tables.
    */
   numPages = pages;
   for (int i = 0; i < MAXPROC; i++)
   {
	   Process *proc = getProc(i);
	   proc->pageTable = malloc(pages * sizeof(PTE));
	   if (proc->pageTable == NULL)
	   {
		   USLOSS_Console("vmInitReal(): Could not malloc page tables.\n");
		   USLOSS_Halt(1);
	   }
	   initPageTable(i);
   }

   /* 
    * Create the fault mailbox or semaphore
    */
   faultMBox = MboxCreate(0, 0);

   /*
    * Fork the pagers.
    */
   for (int i = 0; i < pagers; i++) {
	   pagerPid[i] = fork1("Pager", Pager, NULL, USLOSS_MIN_STACK, PAGER_PRIORITY);
	   if (pagerPid[i] < 0) {
		   USLOSS_Console("vmInitReal(): could not create pager %d\n", i);
		   USLOSS_Halt(1);
	   }
   }

   /*
    * Zero out, then initialize, the vmStats structure
    */
   memset((char *) &vmStats, 0, sizeof(VmStats));
   vmStats.pages = pages;
   vmStats.frames = frames;
   /*
    * Initialize other vmStats fields.
    */

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

   CheckMode();
   USLOSS_MmuDone();
   /*
    * Kill the pagers here.
    */
   /* 
    * Print vm statistics.
    */
   USLOSS_Console("vmStats:\n");
   USLOSS_Console("pages: %d\n", vmStats.pages);
   USLOSS_Console("frames: %d\n", vmStats.frames);
   USLOSS_Console("blocks: %d\n", vmStats.blocks);
   /* and so on... */

} /* vmDestroyReal */


/*
 *----------------------------------------------------------------------
 *
 * FaultHandler
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
             int arg  /* Offset within VM region */)
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
} /* FaultHandler */


/*
 *----------------------------------------------------------------------
 *
 * Pager 
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
    Process *prc = NULL;
    PTE* newPage = NULL;
    FaultMsg *fault;
    int pageNum = 0, frame = 0, pid = 0;
    while(1) {
        /* Wait for fault to occur (receive from mailbox) */
        /* Look for free frame */
        /* If there isn't one then use clock algorithm to
         * replace a page (perhaps write to disk) */
        /* Load page into frame from disk, if necessary */
        /* Unblock waiting (faulting) process */
	MboxReceive(fault_mid, &pid, sizeof(int));
        fault = &faults[pid%MAXPROC];
        if(isZapped())
            break;
        prc = &processes[pid%MAXPROC];
        pageNum = ((long)fault->addr - (long)prc->pageTable) / USLOSS_MmuPageSize();
        newPage =  &(prc->pageTable[pageNum]);
        if (vmStats.freeFrames > 0){
            vmStats.freeFrames--;
            for(frame = 0; frame < vmStats.frames; frame++){
                if (frameTable[frame].state == UNUSED){
                    int result = USLOSS_MmuMap(TAG, pageNum, frame, USLOSS_MMU_PROT_RW);
                    if (result != USLOSS_MMU_OK){
                        USLOSS_Console("Pager():\t mmu map failed in %d, %d\n", pageNum, frame);
                        USLOSS_Halt(1);                       
                    }
                    break;
                }
            }
        }
        else{
            USLOSS_Console("Pager() store to disk:\t not done yet\n");
            USLOSS_Halt(1);
        }
        if(newPage->state == UNUSED){
            memset(vmRegion+pageNum*USLOSS_MmuPageSize(), 0, USLOSS_MmuPageSize());
            vmStats.new++;
        }
        else{
            USLOSS_Console("Pager() load from disk:\t not done yet\n");
            USLOSS_Halt(1);            
        }
        /*update frame table*/
        frameTable[frame].pid = pid;
        frameTable[frame].state = INFRAME;
        frameTable[frame].page = pageNum;

        /*unpdate page table*/
        prc->pageTable[pageNum].state = INFRAME;
        prc->pageTable[pageNum].frame = frame;
        MboxSend(fault->replyMbox, NULL, 0);
    }
    return 0;
} /* Pager */

///////////////////////////////////////////////////////////
//-----------------Utilities-----------------------------//
///////////////////////////////////////////////////////////


void initPageTable(int pid)
{
	Process *proc = getProc(pid);
	for (int i = 0; i < NumPages; i++)
	{
		proc->pageTable[i].state = UNUSED;
		proc->pageTable[i].frame = EMPTY;
		proc->pageTable[i].diskBlock = EMPTY;
	}
}
