#include <usloss.h>
#include <usyscall.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <sems.h>
#include <libuser.h>
#include <string.h>
/* ---------------------------- Prototypes -------------------------------*/
void isKernelMode(char *);
void setUserMode();
void emptyProc(int);
void initProc(int);
void initProcQueue3(procQueue*, int);
void enq3(procQueue*, procPtr3);
procPtr3 deq3(procQueue*);
procPtr3 peek3(procQueue*);
void removeChild3(procQueue*, procPtr3);
void spawn(USLOSS_Sysargs *);
int spawnReal(char *, int(*)(char *), char *, int, int);
int spawnLaunch(char *);
void terminate(USLOSS_Sysargs *);
void terminateReal(int);
void wait(USLOSS_Sysargs *);
int waitReal(int *);
void getTimeOfDay(USLOSS_Sysargs *);
void cpuTime(USLOSS_Sysargs *);
void getPID(USLOSS_Sysargs *);
void nullsys3(USLOSS_Sysargs *);
void semCreate(USLOSS_Sysargs *);
int semCreateHelp(int);
void semP(USLOSS_Sysargs *);
void semV(USLOSS_Sysargs *);
void semFree(USLOSS_Sysargs *);
/* ---------------------------- Globals -------------------------------*/
void (*syscall_vec[MAXSYSCALLS])(USLOSS_Sysargs *args);
semaphore SemTable[MAXSEMS];
procStruct3 ProcTable3[MAXPROC];
int semNum;

int
start2(char *arg)
{
    int pid;
    int status;
    
    //Check kernel mode here.
    isKernelMode("start2");
    
    //Data structure initialization as needed...
    
    // handlesystem call vec
    int i;
    for (i = 0; i < USLOSS_MAX_SYSCALLS; i++) {
        systemCallVec[i] = nullsys3;
    }
    systemCallVec[SYS_SPAWN] = spawn;
    systemCallVec[SYS_WAIT] = wait;
    systemCallVec[SYS_TERMINATE] = terminate;
    systemCallVec[SYS_SEMCREATE] = semCreate;
    systemCallVec[SYS_SEMP] = semP;
    systemCallVec[SYS_SEMV] = semV;
    systemCallVec[SYS_SEMFREE] = semFree;
    systemCallVec[SYS_GETTIMEOFDAY] = getTimeOfDay;
    systemCallVec[SYS_CPUTIME] = cpuTime;
    systemCallVec[SYS_GETPID] = getPID;

    // handle proc table
    for (i = 0; i < MAXPROC; i++) {
        emptyProc(i);
    }

    // handle semaphore table
    for (i = 0; i < MAXSEMS; i++) {
    	SemTable[i].id = -1;
    	SemTable[i].value = -1;
    	SemTable[i].startingValue = -1;
    	SemTable[i].priv_mBoxID = -1;
    	SemTable[i].mutex_mBoxID = -1;
    }

    semNum = 0;


    /*
     * Create first user-level process and wait for it to finish.
     * These are lower-case because they are not system calls;
     * system calls cannot be invoked from kernel mode.
     * Assumes kernel-mode versions of the system calls
     * with lower-case names.  I.e., Spawn is the user-mode function
     * called by the test cases; spawn is the kernel-mode function that
     * is called by the syscallHandler (via the systemCallVec array);s
     * spawnReal is the function that contains the implementation and is
     * called by spawn.
     *
     * Spawn() is in libuser.c.  It invokes USLOSS_Syscall()
     * The system call handler calls a function named spawn() -- note lower
     * case -- that extracts the arguments from the sysargs pointer, and
     * checks them for possible errors.  This function then calls spawnReal().
     *
     * Here, start2 calls spawnReal(), since start2 is in kernel mode.
     *
     * spawnReal() will create the process by using a call to fork1 to
     * create a process executing the code in spawnLaunch().  spawnReal()
     * and spawnLaunch() then coordinate the completion of the phase 3
     * process table entries needed for the new process.  spawnReal() will
     * return to the original caller of Spawn, while spawnLaunch() will
     * begin executing the function passed to Spawn. spawnLaunch() will
     * need to switch to user-mode before allowing user code to execute.
     * spawnReal() will return to spawn(), which will put the return
     * values back into the sysargs pointer, switch to user-mode, and 
     * return to the user code that called Spawn.
     */
    USLOSS_Console("Spawning start3...\n");
    pid = spawnReal("start3", start3, NULL, USLOSS_MIN_STACK, 3);

    /* Call the waitReal version of your wait code here.
     * You call waitReal (rather than Wait) because start2 is running
     * in kernel (not user) mode.
     */
    pid = waitReal(&status);

    USLOSS_Console("Quitting start2...\n");

    quit(pid);
    return -1;
} /* start2 */

void spawn(USLOSS_Sysargs *args)
{
    isKernelMode("spawn");

    int (*func) (char*) = args->arg1;
    char *arg = args->arg2;
    int stackSize = (int)((long)args->arg3);
    int priority = (int)((long)args->arg4);
    char *name = (char*) (args->arg5);

    USLOSS_Console("spawn(): args are: name = %s, stack size = %d, priority = %d\n", name, stackSize, priority);

    long pid = spawnReal(name, func, arg, stackSize, priority);
    long status = 0;

    USLOSS_Console("spawn(): spawnd pid %d\n", pid);

    // check if it's terminate, if yes than terminate
    if (isZapped())
	terminateReal(1);

    //switch to user mode
    setUserMode();

    // set values for Spawn
    args->arg1 = (void *)pid;
    args->arg4 = (void *)status;
}

int spawnReal(char *name, int (*func)(char*), char *arg, int stackSize, int priority)
{
    isKernelMode("spawnReal");

    // fork the process and get its pid
    USLOSS_Console("spawnReal(): forking process %s... \n", name);
    int pid = fork1(name, spawnLaunch, arg, stackSize, priority);
    USLOSS_Console("spawnReal(): forked process name = %s, pid = %d\n", name, pid);
    
    // return -1 if unable to fork
    if(pid < 0)
	return -1;

    procPtr3 child  = &ProcTable3[pid % MAXPROC];
    enq3(&ProcTable3[getpid() % MAXPROC].childrenQueue, child);

    if(child->pid < 0){
	USLOSS_Console("spawnReal(): initializing proc table entry for pid %d\n", pid);
        initProc(pid);        
    }
    
    child->startFunc = func; //set starting function
    child->parentPtr = &ProcTable3[getpid() & MAXPROC]; // set child's parent pointer

    MboxCondSend(child->mboxID, 0, 0);
    return pid;
}

/* Purpose: launches user mode processes and terminates it */
int spawnLaunch(char *startArg) {
    isKernelMode("spawnLaunch");

    USLOSS_Console("spawnLaunch(): launched pid = %d\n", getpid());

    // terminate self if zapped
    if (isZapped())
        terminateReal(1); 

    // get the proc info
    procPtr3 proc = &ProcTable3[getpid() % MAXPROC]; 

    // if spawnReal hasn't done it yet, set up proc table entry
    if (proc->pid < 0) {
        USLOSS_Console("spawnLaunch(): initializing proc table entry for pid %d\n", getpid());
        initProc(getpid());

        // block until spawnReal is done
        MboxReceive(proc->mboxID, 0, 0);
    }

    // switch to user mode
    setUserMode();

    USLOSS_Console("spawnLaunch(): starting process %d...\n", proc->pid);

    // call the function to start the process
    int status = proc->startFunc(startArg);

    USLOSS_Console("spawnLaunch(): terminating process %d with status %d\n", proc->pid, status);

    Terminate(status); // terminate the process if it hasn't terminated itself
    return 0;
}

void wait(USLOSS_Sysargs *args)
{
    isKernelMode("wait");

    int *status = args->arg2;
    int pid = waitReal(status);

    USLOSS_Console("wait(): joined with child pid = %d, status = %d\n", pid, *status);

    args->arg1 = (void *) ((long)(pid));
    args->arg2 = (void *) ((long)*status);
    args->arg4 = (void *) (0);

    // terminate self if zapped
    if (isZapped())
        terminateReal(1); 

    // switch back to user mode
    setUserMode();
}

int waitReal(int *status) 
{
    isKernelMode("waitReal");

    USLOSS_Console("in waitReal\n");
    int pid = join(status);
    return pid;
}


/* initializes proc struct */
void initProc(int pid) {
    isKernelMode("initProc"); 

    int i = pid % MAXPROC;

    ProcTable3[i].pid = pid; 
    ProcTable3[i].mboxID = MboxCreate(0, 0);
    ProcTable3[i].startFunc = NULL;
    ProcTable3[i].nextProcPtr = NULL; 
    initProcQueue3(&ProcTable3[i].childrenQueue, CHILDREN);
}

/* set proc struct to initial*/
void emptyProc(int pid) {
    isKernelMode("emptyProc"); 

    int i = pid % MAXPROC;

    ProcTable3[i].pid = -1; 
    ProcTable3[i].mboxID = -1;
    ProcTable3[i].startFunc = NULL;
    ProcTable3[i].nextProcPtr = NULL; 
}

/* terminate the processes and its children */ 

void terminate(USLOSS_Sysargs *args)
{
    isKernelMode("terminate");

    int status = (int)((long)args->arg1);
    //int status = (int)(args->arg1);
    terminateReal(status);
    // switch back to user mode
    setUserMode();
}

void terminateReal(int status) 
{
    isKernelMode("terminateReal");

    USLOSS_Console("terminateReal(): terminating pid %d, status = %d\n", getpid(), status);

    // zap all children
    procPtr3 proc = &ProcTable3[getpid() % MAXPROC];
    while (proc->childrenQueue.size > 0) {
        procPtr3 child = deq3(&proc->childrenQueue);
        zap(child->pid);
    }
    // remove self from parent's list of children
    removeChild3(&(proc->parentPtr->childrenQueue), proc);
    quit(status);
}

/* ------------------------------------------------------------------------
  Below are functions that manipulate the semaphore.
   ----------------------------------------------------------------------- */

void semCreate (USLOSS_Sysargs *args)
{
    isKernelMode("semCreate");

    int val = (long) args->arg1;
    //int val = args->arg1;
    if(val < 0 || semNum == MAXSEMS){
	args->arg4 = (void*) (long) -1;
       // args->arg4 = (void*) -1;
    }
    else{
	semNum++;
	int temp = semCreateHelp(val);
	args->arg1 = (void*) (long) temp;
        //args->arg1 = (void*) temp;
	args->arg4 = 0;
    }

    if(isZapped()){
	terminateReal(0);
    }
    else{
	setUserMode();
    }
}

int semCreateHelp(int value) {
    isKernelMode("semCreateHelp");

    int i;
    int priv_mBoxID = MboxCreate(value, 0);
    int mutex_mBoxID = MboxCreate(1, 0);

    MboxSend(mutex_mBoxID, NULL, 0);

    for (i = 0; i < MAXSEMS; i++) {
	if (SemTable[i].id == -1) {
	    SemTable[i].id = i;
	    SemTable[i].value = value;
	    SemTable[i].startingValue = value;
	    SemTable[i].priv_mBoxID = priv_mBoxID;
	    SemTable[i].mutex_mBoxID = mutex_mBoxID;
            initProcQueue3(&SemTable[i].blockedProcs, BLOCKED);
	    break;
	}
    }

    int j;
    for (j = 0; j < value; j++) {
	MboxSend(priv_mBoxID, NULL, 0);
    }

    MboxReceive(mutex_mBoxID, NULL, 0);

    return SemTable[i].id;
}

/* sysArgs: getTimeOf Day */
void getTimeOfDay (USLOSS_Sysargs *args)
{
    isKernelMode("getTimeOfDay");
    int time = 0;
    int check = USLOSS_DeviceInput(USLOSS_CLOCK_INT, 0, &time);
    if(check == USLOSS_DEV_INVALID){
        USLOSS_Console("getTimeOfDay(): check invalid, returning\n");
        return;
    }
    *((int *)(args->arg1)) = time;
}

/* sysArgs: CPU time */
void cpuTime(USLOSS_Sysargs *args)
{
    isKernelMode("cpuTime");
    *((int *)(args->arg1)) = readtime();
}

/* sysArgs: getPID */
void getPID(USLOSS_Sysargs *args)
{
    isKernelMode("getPID");
    *((int *)(args->arg1)) = getpid();
}

/* sysArgs: nullsys3 */
void nullsys3(USLOSS_Sysargs *args)
{
    USLOSS_Console("nullsys(): Invalid syscall %d. Terminating...\n", args->number);
    terminateReal(1);
}

/* check kernel mode*/
void isKernelMode(char *name)
{
    if( (USLOSS_PSR_CURRENT_MODE & USLOSS_PsrGet()) == 0 ) {
        USLOSS_Console("%s: called while in user mode, by process %d. Halting...\n", 
             name, getpid());
        USLOSS_Halt(1); 
    }
} 

/* switch to user mode*/
void setUserMode()
{
    int result = USLOSS_PsrSet( USLOSS_PsrGet() & ~USLOSS_PSR_CURRENT_MODE );
    if(result == USLOSS_DEV_INVALID){
	USLOSS_Console("diskHandler(): unit number invalid, returning\n");
            return;
    }
}

/* ------------------------------------------------------------------------
  Below are functions that manipulate the queue data structure.
   ----------------------------------------------------------------------- */

/* Initialize the given procQueue */
void initProcQueue3(procQueue* q, int type) {
  q->head = NULL;
  q->tail = NULL;
  q->size = 0;
  q->type = type;
}

/* Add the given procPtr3 to the back of the given queue. */
void enq3(procQueue* q, procPtr3 p) {
  if (q->head == NULL && q->tail == NULL) {
    q->head = q->tail = p;
  } else {
    if (q->type == BLOCKED)
      q->tail->nextProcPtr = p;
    else if (q->type == CHILDREN)
      q->tail->nextSiblingPtr = p;
    q->tail = p;
  }
  q->size++;
}

/* Remove and return the head of the given queue. */
procPtr3 deq3(procQueue* q) {
  procPtr3 temp = q->head;
  if (q->head == NULL) {
    return NULL;
  }
  if (q->head == q->tail) {
    q->head = q->tail = NULL; 
  }
  else {
    if (q->type == BLOCKED)
      q->head = q->head->nextProcPtr;  
    else if (q->type == CHILDREN)
      q->head = q->head->nextSiblingPtr;  
  }
  q->size--;
  return temp;
}

/* Remove the child process from the queue */
void removeChild3(procQueue* q, procPtr3 child) {
  if (q->head == NULL || q->type != CHILDREN)
    return;

  if (q->head == child) {
    deq3(q);
    return;
  }

  procPtr3 prev = q->head;
  procPtr3 p = q->head->nextSiblingPtr;

  while (p != NULL) {
    if (p == child) {
      if (p == q->tail)
        q->tail = prev;
      else
        prev->nextSiblingPtr = p->nextSiblingPtr->nextSiblingPtr;
      q->size--;
    }
    prev = p;
    p = p->nextSiblingPtr;
  }
}

/* Return the head of the given queue. */
procPtr3 peek3(procQueue* q) {
  if (q->head == NULL) {
    return NULL;
  }
  return q->head;   
}
