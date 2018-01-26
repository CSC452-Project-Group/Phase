/* ------------------------------------------------------------------------
   phase1.c

   University of Arizona
   Computer Science 452
   Fall 2015

   ------------------------------------------------------------------------ */

#include "phase1.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "kernel.h"

/* ------------------------- Prototypes ----------------------------------- */
int sentinel (char *);
void dispatcher(void);
void launch();
static void checkDeadlock();
void disableInterrupts();
void enableInterrupts();
void isKernelMode();
struct procPtr getLastProc(struct procPtr);
struct procPtr getReadyList(int priority);
void moveBack(struct procPtr);
struct procPtr getNextProc();

/* -------------------------- Globals ------------------------------------- */

// Patrick's debugging global variable...
int debugflag = 1;

// the process table
procStruct ProcTable[MAXPROC];

// current size of proctable
static int curProcSize = 0;

//The number of process
int procNum;

// psr bit struct
struct psrBits psr;

// Process lists
static procPtr pr1;
static procPtr pr2;
static procPtr pr3;
static procPtr pr4;
static procPtr pr5;
static procPtr pr6;

// current process ID
procPtr Current;

// the next pid to be assigned
unsigned int nextPid = SENTINELPID;


/* -------------------------- Functions ----------------------------------- */
/* ------------------------------------------------------------------------
   Name - startup
   Purpose - Initializes process lists and clock interrupt vector.
             Start up sentinel process and the test process.
   Parameters - argc and argv passed in by USLOSS
   Returns - nothing
   Side Effects - lots, starts the whole thing
   ----------------------------------------------------------------------- */
void startup(int argc, char *argv[])
{
    int result; /* value returned by call to fork1() */

    /* initialize the process table */
    if (DEBUG && debugflag)
        USLOSS_Console("startup(): initializing process table, ProcTable[]\n");

    // Initizlize the process number
    procNum = 0;

    // Initialize the Ready list, etc.
    if (DEBUG && debugflag)
        USLOSS_Console("startup(): initializing the Ready list\n");
    pr1 = NULL;
    pr2 = NULL;
    pr3 = NULL;
    pr4 = NULL;
    pr5 = NULL;
    pr6 = NULL;
    
    Current = NULL;

    // Initialize the clock interrupt handler

    // startup a sentinel process
    if (DEBUG && debugflag)
        USLOSS_Console("startup(): calling fork1() for sentinel\n");
    result = fork1("sentinel", sentinel, NULL, USLOSS_MIN_STACK,
                    SENTINELPRIORITY);
    if (result < 0) {
        if (DEBUG && debugflag) {
            USLOSS_Console("startup(): fork1 of sentinel returned error, ");
            USLOSS_Console("halting...\n");
        }
        USLOSS_Halt(1);
    }
  
    // start the test process
    if (DEBUG && debugflag)
        USLOSS_Console("startup(): calling fork1() for start1\n");
    result = fork1("start1", start1, NULL, 2 * USLOSS_MIN_STACK, 1);
    if (result < 0) {
        USLOSS_Console("startup(): fork1 for start1 returned an error, ");
        USLOSS_Console("halting...\n");
        USLOSS_Halt(1);
    }

    USLOSS_Console("startup(): Should not see this message! ");
    USLOSS_Console("Returned from fork1 call that created start1\n");

    return;
} /* startup */

/* ------------------------------------------------------------------------
   Name - finish
   Purpose - Required by USLOSS
   Parameters - none
   Returns - nothing
   Side Effects - none
   ----------------------------------------------------------------------- */
void finish(int argc, char *argv[])
{
    if (DEBUG && debugflag)
        USLOSS_Console("in finish...\n");
} /* finish */

/* ------------------------------------------------------------------------
   Name - fork1
   Purpose - Gets a new process from the process table and initializes
             information of the process.  Updates information in the
             parent process to reflect this child process creation.
   Parameters - the process procedure address, the size of the stack and
                the priority to be assigned to the child process.
   Returns - the process id of the created child or -1 if no child could
             be created or if priority is not between max and min priority.
   Side Effects - ReadyList is changed, ProcTable is changed, Current
                  process information changed
   ------------------------------------------------------------------------ */
int fork1(char *name, int (*startFunc)(char *), char *arg,
          int stacksize, int priority)
{
    int procSlot = -1;
    unsigned int pid = 0;

    if (DEBUG && debugflag)
        USLOSS_Console("fork1(): creating process %s\n", name);
    
    // test if in kernel mode; halt if in user mode
	isKernelMode();
    disableInterrupts();

    // Return if stack size is too small
	if (stacksize < USLOSS_MIN_STACK) {
		USLOSS_Console("fork1() : stacksize for process %s too small\n", name); 
		return -2;
	}
	
    // Return if priority is wrong
	if(priority < MAXPRIORITY || prioroty > MINPRIORITY){
		USLOSS_Console("fork1() : priority for process %s is wrong\n", name);
		return -1;	
	}

    // Return if name startFunc is NULL
	if(name == NULL){
		USLOSS_Console("fork1() : name for process %s is NULL\n", name);
		return -1;	
	}
	
    // Return if startFunc is NULL
    if(startFunc == NULL){
      USLOSS_Console("fork1() : startFunc for process %s is NULL\n", name);
      return -1;	
    }	

      // Is there room in the process table? What is the next PID?
    // loop till a pid with a proc slot can be found
    int i;
    for(i = 0; i < 50; i++) {
      if(ProcTable[nextPid%50] == NULL) {
        procSlot = nextPid%50;
        pid = nextPid;
        nextPid++;
        break;
      }
      else if (i == 49) {
        break;
      }
      else {
        nextPid++;
      }
    }

    USLOSS_Console("fork1(): New PID is %d\n", procSlot);
    //No room in the process table, return
    if(procSlot == -1){
      USLOSS_Console("fork1() : No room for process %s\n", name);
      return -1;	
    }

    // fill-in entry in process table */
    if ( strlen(name) >= (MAXNAME - 1) ) {
        USLOSS_Console("fork1(): Process name is too long.  Halting...\n");
        USLOSS_Halt(1);
    }
    strcpy(ProcTable[procSlot].name, name);
    ProcTable[procSlot].startFunc = startFunc;
    if ( arg == NULL )
        ProcTable[procSlot].startArg[0] = '\0';
    else if ( strlen(arg) >= (MAXARG - 1) ) {
        USLOSS_Console("fork1(): argument too long.  Halting...\n");
        USLOSS_Halt(1);
    }
    else
        strcpy(ProcTable[procSlot].startArg, arg);
	
        ProcTable[procSlot].nextProcPtr = NULL;
        ProcTable[procSlot].childProcPtr = NULL;
        ProcTable[procSlot].nextSibling = NULL;
        ProcTable[procSlot].pid = pid;
        ProcTable[procSlot].priority = priority;
        ProcTable[procSlot].stack = malloc(stacksize);
    	if (ProcTable[procSlot].stack == NULL) {
        	USLOSS_Console("fork1() : not enough memory for process %s stack\n");
		USLOSS_Halt(1);
    	}	
        ProcTable[procSlot].stackSize = stacksize;
        ProcTable[procSlot].status = READY;
	
    // Initialize context for this process, but use launch function pointer for
    // the initial value of the process's program counter (PC)
    procNum++;	

    USLOSS_ContextInit(&(ProcTable[procSlot].state),
                       ProcTable[procSlot].stack,
                       ProcTable[procSlot].stackSize,
                       NULL,
                       launch);

    // for future phase(s)
    p1_fork(ProcTable[procSlot].pid);

    getLastProc(getReadyList(priority)) = ProcTable[procSlot]; 

    if (priority != 6) {
        dispatcher();
    }

    enableInterrupts();
    return ProcTable[procSlot].pid;  // -1 is not correct! Here to prevent warning.
} /* fork1 */

/* ------------------------------------------------------------------------
   Name - launch
   Purpose - Dummy function to enable interrupts and launch a given process
             upon startup.
   Parameters - none
   Returns - nothing
   Side Effects - enable interrupts
   ------------------------------------------------------------------------ */
void launch()
{
    int result;

    if (DEBUG && debugflag)
        USLOSS_Console("launch(): started\n");

    // Enable interrupts
    enableInterrupts();

    // Call the function passed to fork1, and capture its return value
    result = Current->startFunc(Current->startArg);

    if (DEBUG && debugflag)
        USLOSS_Console("Process %d returned to launch\n", Current->pid);

    quit(result);

} /* launch */


/* ------------------------------------------------------------------------
   Name - join
   Purpose - Wait for a child process (if one has been forked) to quit.  If 
             one has already quit, don't wait.
   Parameters - a pointer to an int where the termination code of the 
                quitting process is to be stored.
   Returns - the process id of the quitting child joined on.
             -1 if the process was zapped in the join
             -2 if the process has no children
   Side Effects - If no child process has quit before join is called, the 
                  parent is removed from the ready list and blocked.
   ------------------------------------------------------------------------ */
int join(int *status)
{
    // test if its in kernel mode; disable Interrupts
    isKernelMode();
    disableInterrupts();
    
    // Child process:
    procPtr child = Current->childProcPtr;

    if(child != NULL){
        Current->status = BLOCKED;
        dispatcher();
        if ( Current -> quitChildPtr != NULL) {
            *status = Current->quitChildPtr->status;
            Current->quitChildPtr->status = UNUSED;
        }
        else{
            dispatcher();
        }
        enableInterrupts();
        return Current->quitChildPtr->pid;
    }

    enableInterrupts();
    return -1;  // -1 is not correct! Here to prevent warning.
} /* join */


/* ------------------------------------------------------------------------
   Name - quit
   Purpose - Stops the child process and notifies the parent of the death by
             putting child quit info on the parents child completion code
             list.
   Parameters - the code to return to the grieving parent
   Returns - nothing
   Side Effects - changes the parent of pid child completion status list.
   ------------------------------------------------------------------------ */
void quit(int status)
{
    p1_quit(Current->pid);
} /* quit */


/* ------------------------------------------------------------------------
   Name - dispatcher
   Purpose - dispatches ready processes.  The process with the highest
             priority (the first on the ready list) is scheduled to
             run.  The old process is swapped out and the new process
             swapped in.
   Parameters - none
   Returns - nothing
   Side Effects - the context of the machine is changed
   ----------------------------------------------------------------------- */
void dispatcher(void)
{
    //Test for kernel mode
    isKernelMode();
    
    //disable interrupts
    disableInterrupts();
    
    procPtr nextProcess = NULL;
    
    //Check if current is still running, move to the back of the ready list
    if(Current->status == RUNNING) {
        Current->status = READY;
        moveBack(getReadyList(Current->priority));
    }
    
    if(Current->status == BLOCKED) {
        getReadyList(Current->priority) = getReadyList(Current->priority)->nextProcPtr;
    }
    
    nextProcess = getNextProc();
    
    if (Current == NULL) {
        USLOSS_ContextSwitch(NULL, nextProcess);
    } else {
        USLOSS_ContextSwitch(Current, nextProcess);
    }
    Current = nextProcess;
    
    enableInterrupts();
    
} /* dispatcher */


/* ------------------------------------------------------------------------
   Name - sentinel
   Purpose - The purpose of the sentinel routine is two-fold.  One
             responsibility is to keep the system going when all other
             processes are blocked.  The other is to detect and report
             simple deadlock states.
   Parameters - none
   Returns - nothing
   Side Effects -  if system is in deadlock, print appropriate error
                   and halt.
   ----------------------------------------------------------------------- */
int sentinel (char *dummy)
{
    if (DEBUG && debugflag)
        USLOSS_Console("sentinel(): called\n");
    while (1)
    {
        checkDeadlock();
        USLOSS_WaitInt();
    }
} /* sentinel */


/* check to determine if deadlock has occurred... */
static void checkDeadlock()
{
    if (procNum > 1){
        USLOSS_Console("Number of process left: %d, Deadlock appears! Halting...\n", procNum);
        USLOSS_Halt(1);
    }
    else{
        USLOSS_Console("No processes left!\n");
	USLOSS_Halt(0);
    }
} /* checkDeadlock */


/*
 * Disables the interrupts.
 */
void disableInterrupts()
{
    // turn the interrupts OFF iff we are in kernel mode
    // if not in kernel mode, print an error message and
    // halt USLOSS
    
    //Check kernel mode
    isKernelMode();

    int status = USLOSS_PsrSet( USLOSS_PsrGet() & ~USLOSS_PSR_CURRENT_INT );
    if(status == USLOSS_ERR_INVALID_PSR){
        USLOSS_Console("disableInterrupts(): error invalid psr, (halting)");
        USLOSS_Halt(1);
    }


} /* disableInterrupts */

/*
 * Enables the interrupts.
 */
void enableInterrupts()
{
    // turn the interrupts ON iff we are in kernel mode
    // if not in kernel mode, print an error message and
    // halt USLOSS
    isKernelMode();

    int status = USLOSS_PsrSet( USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT );
    if(status == USLOSS_ERR_INVALID_PSR){
        USLOSS_Console("enableInterrupts(): error invalid psr, (halting)");
        USLOSS_Halt(1);
    }
}

/*
 * Checks if currently in kernel mode
 */
void isKernelMode() {
    psr.integerPart	= USLOSS_PsrGet(); // get the usloss psr
    
    if (psr.bits.curMode == 0){
		USLOSS_Console("fork1() : process %s is not in kernel mode\n", name);
		USLOSS_Halt(1);
	}
}

/*
 * returns pointer to last node in the given ready table
 */
struct procPtr getLastProc(struct procPtr head) {
    struct procPtr cur = head;
    
    while (cur != NULL) {
        cur = cur->nextProcPtr;
    }
    
    return cur;
}

/*
 * gets the correct ready list for the priority given
 */
struct procPtr getReadyList(int priority) {
    if (priority == 1) {
        return pr1;
    } else if (priority == 2) {
        return pr2;
    } else if (priority == 3) {
        return pr3;
    } else if (priority == 4) {
        return pr4;
    } else if (priority == 5) {
        return pr5;
    } else if (priority == 6) {
        return pr6;
    } else {
        return NULL;
    }
}

/*
 * moves a process to the back of its ready list
 */
void moveBack(struct procPtr head) {
    struct procPtr cur = head;
    
    while (cur != null) {
        cur = cur->next;
    }
    
    cur = Current;
    cur->nextProcPtr = NULL;
    head = head->nextProcPtr;
}

/*
 * finds and returns the next process
 */
struct procPtr getNextProc() {
    if (pr1 != NULL)
        return pr1;
    else if (pr2 != NULL)
        return pr2;
    else if (pr3 != NULL)
        return pr3;
    else if (pr4 != NULL)
        return pr4;
    else if (pr5 != NULL)
        return pr5;
    else 
        return pr6;
}






