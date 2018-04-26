#include <phase1.h>
/*
 * vm.h
 */


/*
 * All processes use the same tag.
 */
#define TAG 0

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

struct procQueue5 {
	procPtr5 head;
	procPtr5 tail;
	int 	 size;
	int 	 type; /* which procPtr to use for next */
};

/*
* Process struct for phase 5
*/
struct procStruct5 {
	int             pid;
	int 		    mboxID; /* 0 slot mailbox belonging to this process */
	int(*startFunc) (char *);   /* function where process begins */
	procPtr5     	nextProcPtr;
	procPtr5        nextSiblingPtr;
	procPtr5        parentPtr;
	procQueue5 		childrenQueue;
};