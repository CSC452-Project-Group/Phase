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
void isKernelMode(char *);
procPtr getLastProc(procPtr);
procPtr getReadyList(int);
void moveBack(procPtr);
procPtr getNextProc();
void cleanProc(int);
void insertIntoReadyList(procPtr);
void removeFromReadyList(procPtr);
int zap(int);
int isZapped();
void clock_handler(int, void*);
void dumpProcesses();
int procTime();
int getpid();
void illegalInstructionHandler(int dev, void *arg);
/* -------------------------- Globals ------------------------------------- */

// Patrick's debugging global variable...
int debugflag = 1;

// the process table
procStruct ProcTable[MAXPROC];

//The number of process
int procNum;

// Process lists
procPtr pr1;
procPtr pr2;
procPtr pr3;
procPtr pr4;
procPtr pr5;
procPtr pr6;

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
    isKernelMode("startup()");
    int result; /* value returned by call to fork1() */

    /* initialize the process table */
    if (DEBUG && debugflag)
        USLOSS_Console("startup(): initializing process table, ProcTable[]\n");

    // Initizlize the process number
    procNum = 0;

    // Initialize the illegalInstruction interrupt handler

    USLOSS_IntVec[USLOSS_ILLEGAL_INT] = illegalInstructionHandler;

    // Initialize the clock handler
    USLOSS_IntVec[USLOSS_CLOCK_INT] = clock_handler;

    // Initialize the Ready list, etc.
    if (DEBUG && debugflag)
        USLOSS_Console("startup(): initializing the Ready list\n");
    pr1 = NULL;
    pr2 = NULL;
    pr3 = NULL;
    pr4 = NULL;
    pr5 = NULL;
    pr6 = NULL;
    
    // Current Running Process
    Current = NULL;

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

void illegalInstructionHandler(int dev, void *arg)
{
    if (DEBUG && debugflag)
	USLOSS_Console("illegalInstructionHandler() called\n");
}

/* illegalInstructionHandler */

/* ------------------------------------------------------------------------
   Name - finish
   Purpose - Required by USLOSS
   Parameters - none
   Returns - nothing
   Side Effects - none
   ----------------------------------------------------------------------- */
void finish(int argc, char *argv[])
{
    isKernelMode("finish()");
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
	isKernelMode("fork1()");
    disableInterrupts();

    // Return if stack size is too small
	if (stacksize < USLOSS_MIN_STACK) {
		//USLOSS_Console("fork1() : stacksize for process %s too small\n", name); 
		return -2;
	}
	
    // Return if priority is wrong
    if(procNum == 0) {
        //USLOSS_Console("fork1() : process %d for sentinel\n", priority);
    }
	else if(priority < MAXPRIORITY || priority > MINPRIORITY){
		//USLOSS_Console("fork1() : priority for process %s is wrong\n", name);
		return -1;	
	}
    else {
        //USLOSS_Console("fork1() : next priority is %d\n", priority);
    }

    // Return if name startFunc is NULL
	if(name == NULL){
		//USLOSS_Console("fork1() : name for process %s is NULL\n", name);
		return -1;	
	}
	
    // Return if startFunc is NULL
    if(startFunc == NULL){
      //USLOSS_Console("fork1() : startFunc for process %s is NULL\n", name);
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
      //USLOSS_Console("fork1() : No room for process %s\n", name);
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
	
        // Initialization of all ProcStruct fields
        ProcTable[procSlot].nextProcPtr = NULL;
        ProcTable[procSlot].childProcPtr = NULL;
        ProcTable[procSlot].nextSiblingPtr = NULL;
        ProcTable[procSlot].pid = pid;
        ProcTable[procSlot].priority = priority;
        ProcTable[procSlot].procSlot = procSlot;
        ProcTable[procSlot].stack = malloc(stacksize);
    	if (ProcTable[procSlot].stack == NULL) {
        	USLOSS_Console("fork1() : not enough memory for process %s stack\n");
		USLOSS_Halt(1);
    	}	
        ProcTable[procSlot].stackSize = stacksize;
        ProcTable[procSlot].status = READY;
        ProcTable[procSlot].quitChild = NULL;
        ProcTable[procSlot].nextQuitSibling = NULL;
        ProcTable[procSlot].zapProc = NULL;
        ProcTable[procSlot].nextZap = NULL;
        ProcTable[procSlot].zapped = NOT_ZAPPED;
        
        if (Current == NULL) {
            ProcTable[procSlot].parentProcPtr = NULL;
        }
        else {
            ProcTable[procSlot].parentProcPtr = &(*Current);
            if (Current->childProcPtr == NULL) {
                Current->childProcPtr = &(ProcTable[procSlot]);
            }
            else {
                ProcTable[procSlot].nextSiblingPtr = Current->childProcPtr;
                Current->childProcPtr = &(ProcTable[procSlot]);
            }
        }
        
	
    procNum++;	

    USLOSS_ContextInit(&(ProcTable[procSlot].state),
                       ProcTable[procSlot].stack,
                       ProcTable[procSlot].stackSize,
                       NULL,
                       launch);

    // for future phase(s)
    p1_fork(ProcTable[procSlot].pid);

    //USLOSS_Console("fork1() : before readylist insertion\n");
    insertIntoReadyList(&ProcTable[procSlot]);
    //USLOSS_Console("fork1() : after readylist insertion\n");

    if (priority != 6) {
        //USLOSS_Console("fork1() : call to dispatcher\n");
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
    isKernelMode("join()");
    disableInterrupts();
    int pid;
    //USLOSS_Console("Join() : Checking child process (live and died).\n");
    if(Current->childProcPtr == NULL && Current->quitChild == NULL){
        //USLOSS_Console("Join() : No children in the current process.\n");
	*status = 0;
        return -2;
    }

    if(Current->quitChild == NULL){
        //USLOSS_Console("Join() : pid %d is blocked beacuse of no dead child.\n", Current->pid);
        Current->status = BLOCKED;
        enableInterrupts();
        dispatcher();
    }
    
    //USLOSS_Console("join() : process %d has dead children\n", Current->pid);
    *status = Current->quitChild->lastProc;
    pid = Current->quitChild->pid;
    Current->quitChild = Current->quitChild->nextQuitSibling;
    cleanProc(pid);

    //USLOSS_Console("join(): after join\n");
    
    enableInterrupts();
    return pid; 
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
    isKernelMode("quit()");
    disableInterrupts();

    if (Current->childProcPtr != NULL) {
        USLOSS_Console("quit(): process %d, '%s', has active children. Halting...\n", Current->pid, Current->name);
        USLOSS_Halt(1);
    }

    //change current status and store last status
    Current->status = QUIT;
    Current->lastProc = status;
    removeFromReadyList(Current);

    procPtr cur;
    
    if(Current->parentProcPtr != NULL){
        cur = Current->parentProcPtr->childProcPtr;
        //USLOSS_Console("quit() : Current: %d, cur: %d\n", Current->pid, cur->pid);
        if (cur->pid == Current->pid) {
            cur->parentProcPtr->childProcPtr = cur->nextSiblingPtr;
        } else {
            while (cur->nextSiblingPtr->pid != Current->pid) {
                cur = cur->nextSiblingPtr;
            }
            cur->nextSiblingPtr = cur->nextSiblingPtr->nextSiblingPtr;
        }
        
        if (Current->parentProcPtr->quitChild == NULL) {
            //USLOSS_Console("quit() : parent pid: %d\n", Current->parentProcPtr->pid);
            
            Current->parentProcPtr->quitChild = Current;
        } else {
            cur = Current->parentProcPtr->quitChild;
            while (cur->nextQuitSibling != NULL) {
                cur = cur->nextQuitSibling;
            }
            //USLOSS_Console("quit() : Child enters the dead queue for %d\n", Current->parentProcPtr->pid);
            cur->nextQuitSibling = Current;
        }
        
        //USLOSS_Console("quit() : Parents first dead child %d\n", Current->parentProcPtr->quitChild->pid);
        //Unblock parent
        if(Current->parentProcPtr->status == BLOCKED){
            Current->parentProcPtr->status = READY;
            insertIntoReadyList(Current->parentProcPtr);
        }
    }

    procPtr child = Current->quitChild;
    //remove dead children
    //USLOSS_Console("Quit() : %d\n", Current->childProcPtr->pid);
    while(child != NULL){
        USLOSS_Console("Quit() : quitChild for parent %d is %d\n", Current->pid, child->pid);
        procPtr nextChild = child->nextQuitSibling;
        USLOSS_Console("Quit() : before cleanProc\n");
        cleanProc(child->pid);
        child = nextChild;
    } 
    
    // Add any zapping processes back the the ready list and update status
    if(Current->zapped == IS_ZAPPED) {
        //USLOSS_Console("quit(): adding zappers to readyList\n");
        while (Current->zapProc != NULL) {
            if (Current->zapProc->status == ZAPPED) {
                insertIntoReadyList(Current->zapProc);
                Current->zapProc->status = READY;
                
            }
            //USLOSS_Console("name: %s\n", Current->zapProc->name);
            Current->zapProc = Current->zapProc->nextZap;
        }
        
    }
   
    //remove current if no parents
    if(Current->parentProcPtr == NULL){
        //USLOSS_Console("Quit() : clean start1 pid: %d\n", Current->pid);
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
    isKernelMode("dispatcher()");
    
    //disable interrupts
    disableInterrupts();
    
    procPtr nextProcess = NULL;
    
    //dumpProcesses();
    
    //Check if current is still running, move to the back of the ready list
    if(Current == NULL) {
        //USLOSS_Console("Dispatcher() : starting start1()\n");
    }
    else if(Current->status == RUNNING) {
        Current->status = READY;
        moveBack(getReadyList(Current->priority));
    }
    else if(Current->status == BLOCKED) {
        removeFromReadyList(Current);
    }
    
    // TODO: use a method for this
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
    else {
        nextProcess = pr6;
    }
    
    procPtr oldProcess = NULL;
    
    if (Current == NULL) {
        Current = &(ProcTable[1]);
        Current = nextProcess;
        //USLOSS_Console("Dispatcher() : before switch\n");
        p1_switch(-1, nextProcess->pid);
        //USLOSS_Console("Dispatcher() : after switch\n");
        enableInterrupts();
        USLOSS_ContextSwitch(NULL, &(Current->state));
        //USLOSS_Console("Dispatcher() : after contect switch\n");
    } else {
        oldProcess = Current;
        Current = nextProcess;
        oldProcess->totalTime += oldProcess->totalTime == -1 ? procTime() - oldProcess->startTime + 1 : procTime() - oldProcess->startTime;
        p1_switch(oldProcess->pid, Current->pid);
        enableInterrupts();
        USLOSS_ContextSwitch(&(oldProcess->state), &(Current->state));
    }
    
    Current->startTime = procTime(); 
    
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
        //USLOSS_Console("Sentinel() : here I am\n");
        checkDeadlock();
        USLOSS_WaitInt();
    }
} /* sentinel */


/* check to determine if deadlock has occurred... */
static void checkDeadlock()
{
    // If sentinel is not the only running process
    // Then the code is in deadlock
    if (procNum > 1){
        USLOSS_Console("checkDeadlock() : Number of process left: %d, Deadlock appears! Halting...\n", procNum);
        USLOSS_Halt(1);
    }
    else{
        USLOSS_Console("All processes completed.\n");
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
    isKernelMode("disableInterrupts()");

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
    // turn the interrupts ON if we are in kernel mode
    // if not in kernel mode, print an error message and
    // halt USLOSS
    isKernelMode("enableInterrupts()");

    int status = USLOSS_PsrSet( USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT );
    if(status == USLOSS_ERR_INVALID_PSR){
        USLOSS_Console("enableInterrupts(): error invalid psr, (halting)");
        USLOSS_Halt(1);
    }
}

/*
 * Checks if currently in kernel mode
 */
void isKernelMode(char *method) {
    
    // TODO: Figure out the union psr struct
    /*
    psr->integerPart	= USLOSS_PsrGet(); // get the usloss psr
    
    if (psr->bits->curMode == 0){
		USLOSS_Console("fork1() : process %s is not in kernel mode\n", name);
		USLOSS_Halt(1);
        
	}
    */
    
    if (!(USLOSS_PSR_CURRENT_MODE & USLOSS_PsrGet())) {
        USLOSS_Console("%s: called while in user mode, by process %d. Halting...\n", method, Current->pid);
		USLOSS_Halt(1);
    }
}

/*
 * returns pointer to last node in the given ready table
 * NOT IN USE!!!!!!!!!!!!!!
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
    //USLOSS_Console("cleanProc() : cleaning proc %d\n", pid);
    isKernelMode("cleanProc()");
    disableInterrupts();

    int i = (pid % MAXPROC);
    //USLOSS_Console("cleanProc() : cleaning procSlot %d\n", i);

    ProcTable[i].nextProcPtr = NULL;
    ProcTable[i].childProcPtr = NULL;
    ProcTable[i].nextSiblingPtr = NULL;
    ProcTable[i].name[0] = 0;     /* process's name */
    ProcTable[i].startArg[0] = 0;  /* args passed to process */
    //ProcTable[i].pid = -1;               /* process id */
    ProcTable[i].priority = -1;
    ProcTable[i].startFunc = NULL;   /* function where process begins -- launch */
    //free(ProcTable[i].stack);
    ProcTable[i].stack = NULL;
    ProcTable[i].stackSize = -1;
    ProcTable[i].startTime = -1;        /* READY, BLOCKED, QUIT, etc. */
    ProcTable[i].totalTime = -1;
    ProcTable[i].sliceTime = 0;
    ProcTable[i].status = EMPTY;
    ProcTable[i].parentProcPtr = NULL;
    ProcTable[i].lastProc = 0;
    ProcTable[i].quitChild = NULL;
    ProcTable[i].nextQuitSibling = NULL;
    ProcTable[i].zapProc = NULL;

    //USLOSS_Console("cleanProc() : before decrementation of procNum\n");
    procNum--;
    enableInterrupts();

}

/* 
 * moves a process to the back of its ready list
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
    
    isKernelMode("zap()");
    disableInterrupts();

    procPtr proc;
    // Can't zap itself
    if(Current->pid == pid){
        USLOSS_Console("zap() : process %d tried to zap itself. Halting...\n", Current->pid);
        USLOSS_Halt(1);
    }
    
    // Can't zap a proc that doesn't exist
    proc = &ProcTable[pid % MAXPROC];
    if(proc->status == EMPTY || proc->pid != pid){
        USLOSS_Console("zap() : process being zapped does not exist. Halting...\n", Current->pid);
        USLOSS_Halt(1);
    }
    
    // set status and zapped for two processes
    Current->status = ZAPPED;
    proc->zapped = IS_ZAPPED;

    // Add zapper to the zapped list
    if(proc->zapProc == NULL) {
        proc->zapProc = Current;
        removeFromReadyList(Current);
    } 
    else {
        proc = proc->zapProc;
        while (proc->nextZap != NULL) {
            proc = proc->nextZap;
        }
        proc->nextZap = Current;
    }
    
    // zapper is now blocked so call dispatcher
    dispatcher();
    
    return 0; 
}

/*
 * checks zapped status
 */
int isZapped(){
    isKernelMode("isZapped()");
    return Current->zapped;
}

/*
 * Inserts a process into a ready list
 */
void insertIntoReadyList(procPtr proc) {
    
    //USLOSS_Console("insertIntoReadyList() : geting last for readylist %d\n", ProcTable[slot].priority);
    procPtr * cur = NULL;
    
    if (proc->priority == 1) {
        cur = &pr1;
    } else if (proc->priority == 2) {
        cur = &pr2;
    } else if (proc->priority == 3) {
        cur = &pr3;
    } else if (proc->priority == 4) {
        cur = &pr4;
    } else if (proc->priority == 5) {
        cur = &pr5;
    } else if (proc->priority == 6) {
        //USLOSS_Console("insertIntoReadyList() : priority %d\n", ProcTable[slot].priority);
        cur = &pr6;
    } else {
        cur = NULL;
    }
    
    if (*cur == NULL) {
        *cur = &(*proc);
    }
    else {
        while ((*cur)->nextProcPtr != NULL) {
            cur = &(*cur)->nextProcPtr;
        }
        (*cur)->nextProcPtr = &(*proc);
    }
    
    //USLOSS_Console("InstertIntoReayList() : pr6 pid %d\n", pr6->pid);
}

/*
 * Removes a process from a ready list and moves the next forward
 */
void removeFromReadyList(procPtr proc) {
    //procPtr list = getReadyList(ProcTable[slot].priority);
    
    procPtr * cur = NULL;
    
    if (proc->priority == 1) {
        cur = &pr1;
    } else if (proc->priority == 2) {
        cur = &pr2;
    } else if (proc->priority == 3) {
        cur = &pr3;
    } else if (proc->priority == 4) {
        cur = &pr4;
    } else if (proc->priority == 5) {
        cur = &pr5;
    } else if (proc->priority == 6) {
        cur = &pr6;
    } else {
        cur = NULL;
    }
    
    if((*cur)->pid == proc->pid) {
        //USLOSS_Console("removeFromReadyList(): Removing %d from list %d\n", proc->pid, proc->priority);
        *cur = &(*((*cur)->nextProcPtr));
        proc->nextProcPtr = NULL;
    }
    else {
        while ((*cur)->nextProcPtr->pid != proc->pid) {
            cur = &(*cur)->nextProcPtr;
        }
        (*cur)->nextProcPtr = proc->nextProcPtr;
    }
    
}

/*
 * Handles the clock interupt
 */
void clock_handler(int dev, void *arg){
    static int count = 0;
    count++;
    USLOSS_Console("Call clockhandler for: %d times\n", count);

    isKernelMode("clock_handler()");
    //disableInterrupts();
   
    if(procTime() - Current->sliceTime >= TIMESLICE){ //Over 80000
	USLOSS_Console("clockHandler(): time slicing\n");
	//Current->sliceTime = 0;
	dispatcher();
    }
    //else{
    //	enableInterrupts();
    //}
}

int procTime(void){
    isKernelMode("procTime()");
    int status = 0;
    int out = USLOSS_DeviceInput(USLOSS_CLOCK_DEV, 0, &status);

    if(out != USLOSS_DEV_OK){
	USLOSS_Console("clock_handler(): failing, halting......");
	USLOSS_Halt(1);
    }

    return status;
}

void dumpProcesses() {
    procPtr cur;
    if (pr1 != NULL) {
        cur = pr1;
        while (cur != NULL) {
            USLOSS_Console("---------->DumpProcesses() : Priority 1 pid: %d\n", cur->pid);
            cur = cur->nextProcPtr;
        }
    }
    if (pr2 != NULL) {
        cur = pr2;
        while (cur != NULL) {
            USLOSS_Console("---------->DumpProcesses() : Priority 2 pid: %d\n", cur->pid);
            cur = cur->nextProcPtr;
        }
    }
    if (pr3 != NULL) {
        cur = pr3;
        while (cur != NULL) {
            USLOSS_Console("---------->DumpProcesses() : Priority 3 pid: %d\n", cur->pid);
            cur = cur->nextProcPtr;
        }
    }
    if (pr4 != NULL) {
        cur = pr4;
        while (cur != NULL) {
            USLOSS_Console("---------->DumpProcesses() : Priority 4 pid: %d\n", cur->pid);
            cur = cur->nextProcPtr;
        }
    }
    if (pr5 != NULL) {
        cur = pr5;
        while (cur != NULL) {
            USLOSS_Console("---------->DumpProcesses() : Priority 5 pid: %d\n", cur->pid);
            cur = cur->nextProcPtr;
        }
    }
    if (pr6 != NULL) {
        cur = pr6;
        while (cur != NULL) {
            USLOSS_Console("---------->DumpProcesses() : Priority 6 pid: %d\n", cur->pid);
            cur = cur->nextProcPtr;
        }
    }
    
    /*
    USLOSS_Console("PID	Parent	Priority	Status		# Kids	CPUtime	Name\n");
    
    int i;
    for(i = 0; i < 50; i++) {
        
        USLOSS_Console("%d	%d   %d 	%d		%s\n", ProcTable[i].pid, 
                        ProcTable[i].parentProcPtr->pid, ProcTable[i].status, ProcTable[i].name);
    }
    */
}

// returns current procs pid
int getpid() {
    return Current->pid;
}

// blocks self
int blockMe(int block_status)
{
    Current->status = block_status;
    removeFromReadyList(Current);
    if(block_status <= 10){
        USLOSS_Console("blockMe(): newStatus must be greater than 10;. Halting...\n");
        USLOSS_Halt(1);
    }
    dispatcher();

    if(isZapped()){
        return -1;
    }else{
        return 0;
    }
}

// unblocks a proc
int unblockProc(int pid)
{

    if(pid < 1){
        return -2;
    }
    procPtr tar = &ProcTable[pid % MAXPROC];
    if(tar->status == EMPTY)
        return -2;
    if (tar->pid != pid)
        return -2;
    if(tar == Current)
        return -2;
    if(tar->status == ZAPPED || tar->status == BLOCKED)
        return -2;

    if(tar->status > 10){
        insertIntoReadyList(tar);
        tar->status = READY;
        dispatcher();
        return 0;
    }else{
        return -2;
    }
}

