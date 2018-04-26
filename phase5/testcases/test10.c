/*
 * test10.c
 *
 * Two processes. Two pages of virtual memory. Two frames.
 * Each writing and reading data from both page 0 and page 1 with
 *    a context switch in between.
 * Should cause page 0 of each process to be written to disk
 * ChildA writes to page 0, in frame 0
 * ChildB writes to page 0, in frame 1
 * ChildA writes to page 1, in frame 0, sends A's page 0 to disk
 * ChildB writes to page 1, in frame 1, sends B's page 0 to disk
 */

#include <usloss.h>
#include <usyscall.h>
#include <phase5.h>
#include <libuser.h>
#include <string.h>
#include <assert.h>

#define Tconsole USLOSS_Console

#define TEST        "test10"
#define PAGES       2
#define CHILDREN    2
#define FRAMES      2
#define PRIORITY    5
#define ITERATIONS  2
#define PAGERS      1
#define MAPPINGS    PAGES

extern void *vmRegion;

extern void printPageTable(int pid);
extern void printFrameTable();

int sem;

void test_setup(int argc, char *argv[])
{
}

void test_cleanup(int argc, char *argv[])
{
}

int
Child(char *arg)
{
    int    pid;
    char   toPrint[64];
    

    GetPID(&pid);
    Tconsole("\nChild(%d): starting\n", pid);
    
    for (int i = 0; i < ITERATIONS; i++) {

        switch (i) {
        case 0:
            sprintf(toPrint, "%c: This is page zero, pid = %d", *arg, pid);
            break;
        case 1:
            sprintf(toPrint, "%c: This is page one, pid = %d", *arg, pid);
            break;
        }
        Tconsole("Child(%d): toPrint = '%s'\n", pid, toPrint);
        Tconsole("Child(%d): strlen(toPrint) = %d\n", pid, strlen(toPrint));

        // memcpy causes a page fault
        memcpy(vmRegion + i*USLOSS_MmuPageSize(), toPrint,
               strlen(toPrint)+1);  // +1 to copy nul character

        Tconsole("Child(%d): after memcpy on iteration %d\n", pid, i);

        if (strcmp(vmRegion + i*USLOSS_MmuPageSize(), toPrint) == 0)
            Tconsole("Child(%d): strcmp first attempt to read worked!\n", pid);
        else {
            Tconsole("Child(%d): Wrong string read, first attempt to read\n",
                     pid);
            Tconsole("  read: '%s'\n", vmRegion + i*USLOSS_MmuPageSize());
            USLOSS_Halt(1);
        }

        SemV(sem);  // to force a context switch

        if (strcmp(vmRegion + i*USLOSS_MmuPageSize(), toPrint) == 0)
            Tconsole("Child(%d): strcmp second attempt to read worked!\n", pid);
        else {
            Tconsole("Child(%d): Wrong string read, second attempt to read\n",
                     pid);
            Tconsole("  read: '%s'\n", vmRegion + i*USLOSS_MmuPageSize());
            USLOSS_Halt(1);
        }

    } // end loop for i

    Tconsole("Child(%d): checking various vmStats\n", pid);
    Tconsole("Child(%d): terminating\n\n", pid);
    Terminate(137);
    return 0;
} /* Child */


int
start5(char *arg)
{
    int  pid[CHILDREN];
    int  status;
    char toPass;
    char buffer[20];

    Tconsole("start5(): Running:    %s\n", TEST);
    Tconsole("start5(): Pagers:     %d\n", PAGERS);
    Tconsole("          Mappings:   %d\n", MAPPINGS);
    Tconsole("          Pages:      %d\n", PAGES);
    Tconsole("          Frames:     %d\n", FRAMES);
    Tconsole("          Children:   %d\n", CHILDREN);
    Tconsole("          Iterations: %d\n", ITERATIONS);
    Tconsole("          Priority:   %d\n", PRIORITY);

    status = VmInit( MAPPINGS, PAGES, FRAMES, PAGERS, &vmRegion );
    Tconsole("start5(): after call to VmInit, status = %d\n\n", status);
    assert(status == 0);
    assert(vmRegion != NULL);

    SemCreate(0, &sem);

    toPass = 'A';
    for (int i = 0; i < CHILDREN; i++) {
        sprintf(buffer, "Child%c", toPass);
        Spawn(buffer, Child, &toPass, USLOSS_MIN_STACK * 7, PRIORITY, &pid[i]);
        toPass = toPass + 1;
    }

    // One P operation per (CHILDREN * ITERATIONS)
    for (int i = 0; i < ITERATIONS*CHILDREN; i++)
        SemP( sem);

    for (int i = 0; i < CHILDREN; i++) {
        Wait(&pid[i], &status);
        assert(status == 137);
    }

    //PrintStats();
    assert(vmStats.faults == 4);
    assert(vmStats.new == 4);
    assert(vmStats.pageOuts == 2);
    assert(vmStats.pageIns == 0);

    Tconsole("start5(): done\n");
    //PrintStats();
    VmDestroy();
    Terminate(1);

    return 0;
} /* start5 */
