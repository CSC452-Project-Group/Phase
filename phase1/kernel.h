/* Patrick's DEBUG printing constant... */
#define DEBUG 0

typedef struct procStruct procStruct;

typedef struct procStruct * procPtr;

struct procStruct {
   procPtr         nextProcPtr;
   procPtr         childProcPtr;
   procPtr         nextSiblingPtr;
   char            name[MAXNAME];     /* process's name */
   char            startArg[MAXARG];  /* args passed to process */
   USLOSS_Context  state;             /* current context for process */
   short           pid;               /* process id */
   int             priority;
   int (* startFunc) (char *);   /* function where process begins -- launch */
   char           *stack;
   unsigned int    stackSize;
   int             status;        /* READY, BLOCKED, QUIT, etc. */
   /* other fields as needed... */
   procPtr         parentProcPtr;
   int             lastProc;
   procPtr         quitChild;
   procPtr         nextQuitSibling;
   int             procSlot;
   int             zapped;
   procPtr         zapProc; 
   procPtr         nextZap;
   int             startTime; //the time when current time slice started
   int             totalTime; //the time of the process has been running
   int             sliceTime; //the time of how long the process has be in this time slice
};

struct psrBits {
    unsigned int curMode:1;
    unsigned int curIntEnable:1;
    unsigned int prevMode:1;
    unsigned int prevIntEnable:1;
    unsigned int unused:28;
};

union psrValues {
   struct psrBits bits;
   unsigned int integerPart;
};

/* Some useful constants.  Add more as needed... */
#define NO_CURRENT_PROCESS NULL
#define MINPRIORITY 5
#define MAXPRIORITY 1
#define SENTINELPID 1
#define SENTINELPRIORITY 6
#define TIMESLICE    80000

#define EMPTY   0
#define READY   1
#define RUNNING 2
#define QUIT    4
#define BLOCKED 5
#define ZAPPED  6

#define IS_ZAPPED  1
#define NOT_ZAPPED 2
