/*
 * test09.c
 *
 * Two processes.  2 pages, 2 frames
 * Each writing and reading data from page 0
 * No disk I/O should occur.  0 replaced pages and 2 page faults
 */

#include <usloss.h>
#include <usyscall.h>
#include <phase5.h>
#include <libuser.h>
#include <string.h>
#include <assert.h>

#define Tconsole USLOSS_Console

#define TEST        "test09"
#define PAGES       2
#define CHILDREN    2
#define FRAMES      2
#define PRIORITY    5
#define ITERATIONS  1
#define PAGERS      1
#define MAPPINGS    PAGES

extern void *vmRegion;

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
    char   str[64];

    GetPID(&pid);
    Tconsole("\nChild(%d): starting\n", pid);

             //             1         2          3         4
             //   012345678901234567890123456787901234567890
    sprintf(str, "This is the first page for pid %d\n", pid);

    Tconsole("Child(%d): str = %s\n", pid, str);
    Tconsole("Child(%d): strlen(str) = %d\n", pid, strlen(str));

    memcpy(vmRegion, str, strlen(str)+1);  // +1 to copy nul character

    Tconsole("Child(%d): after memcpy\n", pid);

    if (strcmp(vmRegion, str) == 0)
        Tconsole("Child(%d): strcmp first attempt to read worked!\n", pid);
    else
        Tconsole("Child(%d): Wrong string read, first attempt to read\n", pid);

    SemV(sem); // to force context switch

    if (strcmp(vmRegion, str) == 0)
        Tconsole("Child(%d): strcmp second attempt to read worked!\n", pid);
    else
        Tconsole("Child(%d): Wrong string read, second attempt to read\n", pid);

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
    char childName[20], letter;

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

    letter = 'A';
    for (int i = 0; i < CHILDREN; i++) {
        sprintf(childName, "Child%c", letter++);
        Spawn(childName, Child, NULL, USLOSS_MIN_STACK*7, PRIORITY, &pid[i]);
    }

    // One P operation per child
    // Both children are low priority, so we want the parent to wait until
    // each child gets past their first strcpy into the VM region.
    for (int i = 0; i < CHILDREN; i++)
        SemP( sem);

    for (int i = 0; i < CHILDREN; i++) {
        Wait(&pid[i], &status);
        assert(status == 137);
    }

    assert(vmStats.faults == 2);
    assert(vmStats.new == 2);
    assert(vmStats.pageOuts == 0);
    assert(vmStats.pageIns == 0);

    Tconsole("start5(): done\n");
    VmDestroy();
    Terminate(1);

    return 0;
} /* start5 */
