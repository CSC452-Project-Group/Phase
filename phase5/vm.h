#include <phase1.h>
/*
 * vm.h
 */


/*
 * All processes use the same tag.
 */
#define TAG 0
#define SWAPDISK 1
/*
 * Different states for a page.
 */
#define UNUSED 500
#define INCORE 501
#define INFRAME 502
/* You'll probably want more states */


/*
 * Page table entry.
 */
typedef struct PTE {
    int  state;      // See above.
    int  frame;      // Frame that stores the page (if any). -1 if none.
    int  diskBlock;  // Disk block that stores the page (if any). -1 if none.
    // Add more stuff here
    int track;
    int sector;
} PTE;

/*
 * Frame table entry
 */
typedef struct FTE {
    int pid;
    int state;
    int page;
} FTE;

/*
 * Per-process information.
 */
typedef struct Process {
    int  numPages;   // Size of the page table.
    PTE  *pageTable; // The page table for the process.
} Process;

/*
 * Information about page faults. This message is sent by the faulting
 * process to the pager to request that the fault be handled.
 */
typedef struct FaultMsg {
    int  pid;        // Process with the problem.
    void *addr;      // Address that caused the fault.
    int  replyMbox;  // Mailbox to send reply.
    // Add more stuff here.
} FaultMsg;

Process processes[MAXPROC];
