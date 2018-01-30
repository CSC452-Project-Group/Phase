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
procPtr getLastProc(procPtr);
procPtr getReadyList(int);
void moveBack(procPtr);
procPtr getNextProc();
void cleanProc(int);
void insertIntoReadyList();
void removeFromReadyList();
int zap(int);
int isZapped();


/* -------------------------- Globals ------------------------------------- */

// Patrick's debugging global variable...
int debugflag = 1;

// the process table
procStruct ProcTable[MAXPROC];

//The number of process
int procNum;

// psr bit struct
//struct psrValues psr;

// Process lists
procPtr pr1;
procPtr pr2;
procPtr pr3;
procPtr pr4;
procPtr pr5;
procPtr pr6;

// current process ID
procPtr Current;

// zapped process
procPtr zapProc;

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
    zapProc = NULL;
    // Initialize the clock interrupt handler

    // startup a sentinel process
    if (DEBUG && debugflag)
        USLOSS_Console("startup(): calling fork1() for sentinel\n");
    //USLOSS_Console("startup(): calling fork1() for sentinel with priority %d\n", SENTINELPRIORITY);
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
        USLOSS_Console("fork1() : creating process %s\n", name);
    
    // test if in kernel mode; halt if in user mode
	isKernelMode();
    disableInterrupts();

    // Return if stack size is too small
	if (stacksize < USLOSS_MIN_STACK) {
		USLOSS_Console("fork1() : stacksize for process %s too small\n", name); 
		return -2;
	}
	
    // Return if priority is wrong
    if(procNum == 0) {
        //USLOSS_Console("fork1() : process %d for sentinel\n", priority);
    }
	else if(priority < MAXPRIORITY || priority > MINPRIORITY){
		USLOSS_Console("fork1() : priority for process %s is wrong\n", name);
		return -1;	
	}
    else {
        //USLOSS_Console("fork1() : next priority is %d\n", priority);
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
      if(ProcTable[nextPid%50].status == EMPTY) {
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

    //USLOSS_Console("fork1(): New PID is %d\n", procSlot);
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
    //USLOSS_Console("fork1() : assigned name %s\n", ProcTable[procSlot].name);
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
        ProcTable[procSlot].nextSiblingPtr = NULL;
        ProcTable[procSlot].pid = pid;
        ProcTable[procSlot].priority = priority;
        ProcTable[procSlot].procSlot = procSlot;
        //USLOSS_Console("fork1() : assigned priority %d\n", ProcTable[procSlot].priority);
        ProcTable[procSlot].stack = malloc(stacksize);
    	if (ProcTable[procSlot].stack == NULL) {
        	USLOSS_Console("fork1() : not enough memory for process %s stack\n");
		USLOSS_Halt(1);
    	}	
        //USLOSS_Console("for1() : setting stack\n");
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

    //USLOSS_Console("fork1() : before readylist insertion\n");
    insertIntoReadyList(procSlot);
    USLOSS_Console("fork1() : after readylist insertion\n");

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
    procPtr curquit=Current->quitChild;
    int pid;
    USLOSS_Console("Checking child process (live and died).\n");
    //TODO:check whether it has children, if no, return -2
    if(Current->childProcPtr == NULL && Current->quitChild == NULL){
        USLOSS_Console("No children in the current process.\n");
	*status = 0;
        return -2;
    }

    //TODO:check if current has dead child. If no, block itself and wait
    if(Current->quitChild == NULL){
	USLOSS_Console("pid %d is blocked beacuse of no dead child.\n", Current->pid);
	Current->status = BLOCKED;
	dispatcher();
	disableInterrupts();
	//removeFromReadyList(Current->procSlot);
	curquit = Current->quitChild;
	Current->quitChild = curquit->nextProcPtr;
	*status = curquit->lastProc;
	pid = curquit->pid;
	curquit->pid = -1;
	curquit->lastProc = 0;
	curquit->status = EMPTY;
	//dispatcher();	
    }
    else{
	Current->quitChild = curquit->nextProcPtr;
	cleanProc(Current->quitChild->pid);
	procPtr child = Current->quitChild; //get first dead child from the queue
        pid = child->pid;
        *status = child->lastProc;
    }

    /*procPtr child = Current->quitChild; //get first dead child from the queue
    pid = child->pid;
    *status = child->lastProc;
    */
    //Current->quitChild = Current->quitChild->nextQuitSibling;

    //cleanProc(pid);

    //check the zapped proc, if any return -1
    if(Current->zapProc != NULL){
	pid = -1;
    }
    enableInterrupts();
    return pid;  // -1 is not correct! Here to prevent warning.
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
    //check if its in kernel mode
    isKernelMode();
    disableInterrupts();

    //TODO:Halt when the quiting process has active children process
    //We need loop through the children queue to find any active process

    //change current status and store last status
    Current->status = QUIT;
    Current->lastProc = status;
    removeFromReadyList(Current->procSlot);

    procPtr cur;
    
    if(Current->parentProcPtr != NULL){
        cur = Current->parentProcPtr->childProcPtr;
        
        if (cur == Current) {
            cur->parentProcPtr->childProcPtr = cur->nextSiblingPtr;
        } else {
            while (cur->nextSiblingPtr != Current) {
                cur = cur->nextSiblingPtr;
            }
            cur->nextSiblingPtr = cur->nextSiblingPtr->nextSiblingPtr;
        }
        
        if (Current->parentProcPtr->quitChild == NULL) {
            Current->parentProcPtr->quitChild = Current;
        } else {
            cur = Current->parentProcPtr->quitChild;
            while (cur != NULL) {
                cur = cur->nextQuitSibling;
            }
            cur = Current;
        }
        

        //Unblock parent
        if(Current->parentProcPtr->status == BLOCKED){
	    Current->parentProcPtr->status = READY;
	    insertIntoReadyList(Current->parentProcPtr->procSlot);
        }
    }

    procPtr child = Current->quitChild;
    //remove dead children
    while(child != NULL){
        procPtr nextChild = child->nextQuitSibling;
        cleanProc(child->pid);
        child = nextChild;
    } 
   
    //remove current if no parents
    if(Current->parentProcPtr == NULL){
	cleanProc(Current->pid);
    }
    
    p1_quit(Current->pid);
    
    //run next process
    dispatcher();
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
    if(Current == NULL) {
        USLOSS_Console("Dispatcher() : starting start1()\n");
    }
    else if(Current->status == RUNNING) {
        Current->status = READY;
        moveBack(getReadyList(Current->priority));
    }
    else if(Current->status == BLOCKED) {
        removeFromReadyList(Current->procSlot);
    }
    
    if (pr1 != NULL)
        nextProcess = pr1;
    else if (pr2 != NULL)
        nextProcess = pr2;
    else if (pr3 != NULL)
        nextProcess = pr3;
    else if (pr4 != NULL)
        nextProcess = pr4;
    else if (pr5 != NULL)
        nextProcess = pr5;
    else 
        nextProcess = pr6;
    
    //nextProcess = getNextProc();
    USLOSS_Console("Dispatcher() : next proc assigned - %d\n", nextProcess->pid);
    procPtr oldProcess;
    
    if (Current == NULL) {
        Current = nextProcess;
        //USLOSS_Console("Dispatcher() : before switch\n");
        p1_switch(-1, nextProcess->pid);
        USLOSS_Console("Dispatcher() : after switch\n");
        enableInterrupts();
        USLOSS_ContextSwitch(NULL, &(Current->state));
        USLOSS_Console("Dispatcher() : after contect switch\n");
    } else {
        oldProcess = Current;
        Current = nextProcess;
        p1_switch(oldProcess->pid, Current->pid);
        enableInterrupts();
        USLOSS_ContextSwitch(&(oldProcess->state), &(Current->state));
    }
    //Current = nextProcess;
    
    //enableInterrupts();
    
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
    
    /*
    psr->integerPart	= USLOSS_PsrGet(); // get the usloss psr
    
    if (psr->bits->curMode == 0){
		USLOSS_Console("fork1() : process %s is not in kernel mode\n", name);
		USLOSS_Halt(1);
        
	}
    */
    
    if ((USLOSS_PSR_CURRENT_MODE & USLOSS_PsrGet()) == 0) {
        USLOSS_Console("fork1() : process is not in kernel mode\n");
		USLOSS_Halt(1);
    }
}

/*
 * returns pointer to last node in the given ready table
 */
procPtr getLastProc(procPtr head) {
    procPtr cur = head;
    
    USLOSS_Console("getLastProc() : getting last in table %d\n", cur->priority);
    while (cur->nextProcPtr != NULL) {
        cur = cur->nextProcPtr;
    }
    
    return cur;
}

/*
 * gets the correct ready list for the priority given
 */
procPtr getReadyList(int priority) {
    USLOSS_Console("getReadyList() : getting readylist %d\n", priority);
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
        USLOSS_Console("getReadyList() : returning readylist 6\n");
        return pr6;
    } else {
        return NULL;
    }
}

/*

* CLean out the process
*/
void cleanProc(int pid){
    isKernelMode();
    disableInterrupts();

    int i = pid % MAXPROC;

    ProcTable[i].nextProcPtr = NULL;
    ProcTable[i].childProcPtr = NULL;
    ProcTable[i].nextSiblingPtr = NULL;
    ProcTable[i].name[0] = 0;     /* process's name */
    ProcTable[i].startArg[0] = 0;  /* args passed to process */
    ProcTable[i].pid = -1;               /* process id */
    ProcTable[i].priority = -1;
    ProcTable[i].startFunc = NULL;   /* function where process begins -- launch */
    free(ProcTable[i].stack);
    ProcTable[i].stack = NULL;
    ProcTable[i].stackSize = -1;
    ProcTable[i].status = EMPTY;        /* READY, BLOCKED, QUIT, etc. */

    procNum--;
    enableInterrupts();

}

/* moves a process to the back of its ready list
 */
void moveBack(procPtr head) {
    procPtr cur = head;
    
    while (cur != NULL) {
        cur = cur->nextProcPtr;
    }
    
    cur = Current;
    cur->nextProcPtr = NULL;
    head = head->nextProcPtr;
}

/*
 * finds and returns the next process
 */
procPtr getNextProc() {
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

/*
* zap
* return -1 if zapped process called by itself.
* return 0 if zapped process has quited.
*/
int zap(int pid){
    
    isKernelMode();
    disableInterrupts();

    procPtr proc;
    if(Current->pid == pid){
        USLOSS_Console("zap() : process is zapping self\n");
        USLOSS_Halt(1);
    }
    
    proc = &ProcTable[pid % MAXPROC];
    if(proc->status == EMPTY || proc->pid != pid){
        USLOSS_Console("zap() : no process to zap with this pid %d\n", pid);
        USLOSS_Halt(1);
    }

    if(proc->status == QUIT){
        enableInterrupts();
        //TODO: We might need a queue for zap here
        if(Current->zapProc == NULL){
            return 0;
        }
        else{
            return -1;
        }
    }

    //TODO:Put Current into zap queue
    Current->status = ZAPPED; 
    removeFromReadyList(proc);
    dispatcher();

    enableInterrupts();

    //TODO: zapQueue
    if (Current->zapProc != NULL) {
        return -1;  
    }
    return 0; 
}

//TODO:Zapqueue
int isZapped(){
    isKernelMode();
    return (Current->zapProc != NULL);
}

/*
 * Inserts a process into a ready list
 */
void insertIntoReadyList(int slot) {
    //USLOSS_Console("insertIntoReadyList() : geting last for readylist %d\n", ProcTable[slot].priority);
    procPtr * cur = NULL;
    
    if (ProcTable[slot].priority == 1) {
        cur = &pr1;
    } else if (ProcTable[slot].priority == 2) {
        cur = &pr2;
    } else if (ProcTable[slot].priority == 3) {
        cur = &pr3;
    } else if (ProcTable[slot].priority == 4) {
        cur = &pr4;
    } else if (ProcTable[slot].priority == 5) {
        cur = &pr5;
    } else if (ProcTable[slot].priority == 6) {
        //USLOSS_Console("insertIntoReadyList() : priority %d\n", ProcTable[slot].priority);
        cur = &pr6;
    } else {
        cur = NULL;
    }
    
    while (*cur != NULL) {
        *cur = (*cur)->nextProcPtr;
    }
    
    //procPtr list = getLastProc(getReadyList(ProcTable[slot].priority));
    //USLOSS_Console("insertIntoReadyList() : after getLastProc()\n");
    *cur = &ProcTable[slot];
    
    //USLOSS_Console("InstertIntoReayList() : pr6 pid %d\n", pr6->pid);
}

/*
 * Removes a process from a ready list and moves the next forward
 */
void removeFromReadyList(int slot) {
    //procPtr list = getReadyList(ProcTable[slot].priority);
    
    procPtr cur = NULL;
    
    if (ProcTable[slot].priority == 1) {
        cur = pr1;
    } else if (ProcTable[slot].priority == 2) {
        cur = pr2;
    } else if (ProcTable[slot].priority == 3) {
        cur = pr3;
    } else if (ProcTable[slot].priority == 4) {
        cur = pr4;
    } else if (ProcTable[slot].priority == 5) {
        cur = pr5;
    } else if (ProcTable[slot].priority == 6) {
        cur = pr6;
    } else {
        cur = NULL;
    }
    
    cur = cur->nextProcPtr;
    ProcTable[slot].nextProcPtr = NULL;
}
