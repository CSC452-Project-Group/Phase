/*
 *  File:  libuser.c
 *
 *  Description:  This file contains the interface declarations
 *                to the OS kernel support package.
 *
 */

#include <usloss.h>
#include <usyscall.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <phase4.h>
#include <libuser.h>

#define CHECKMODE {    \
    if (USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) { \
        USLOSS_Console("Trying to invoke syscall from kernel\n"); \
        USLOSS_Halt(1);  \
    }  \
}

// Phase 4

/*
 *  Routine:  Sleep
 *
 *  Description: Delays the calling process for the specified number of seconds.
 *
 *  Arguments:  int seconds -- number of seconds to delay the process.
 *
 *  Return Value: 0 means success, -1 means error occurs 
 *
 */
int Sleep(int seconds) 
{
    USLOSS_Sysargs sysArg;

    CHECKMODE;
    sysArg.number = SYS_SLEEP;
    sysArg.arg1 = (void *)(long)seconds;

    USLOSS_Syscall(&sysArg);

    return (int)(long)sysArg.arg4;
}

/*
 *  Routine:  DiskRead
 *
 *  Description: Reads one or more sectors from a disk.
 *
 *  Arguments:  void *dbuff -- the memory address to which to transfer
 *              int unit -- the unit number of the disk from which to read
 *              int track -- the starting disk track number
 *              int first -- the starting disk sector number 
 *              int sectors -- number of sectors to read
 *              int *status -- the pointer to the status of the completing child
 *
 *  Return Value: 0 means success, -1 means error occurs
 *
 */
int DiskRead(void *dbuff, int unit, int track, int first, int sectors, int *status)
{
    USLOSS_Sysargs sysArg;
    
    CHECKMODE;
    sysArg.number = SYS_DISKREAD;
    sysArg.arg1 = dbuff;
    sysArg.arg2 = (void *)(long) sectors;
    sysArg.arg3 = (void *)(long) track;
    sysArg.arg4 = (void *)(long) first;
    sysArg.arg5 = (void *)(long) unit;

    USLOSS_Syscall(&sysArg);

    *status = (long) sysArg.arg1;
    return (int)(long) sysArg.arg4;
}

/*
 *  Routine:  DiskWrite
 *
 *  Description: Writes sectors sectors to the disk indicated by unit, starting 
 *               at track track and sector first
 *
 *  Arguments:  void *dbuff -- the memory address to which to transfer
 *              int unit -- the unit number of the disk from which to read
 *              int track -- the starting disk track number
 *              int first -- the starting disk sector number 
 *              int sectors -- number of sectors to read
 *              int *status -- the pointer to the status of the completing child 
 *
 *  Return Value: 0 means success, -1 means error occurs
 *
 */
int DiskWrite(void *dbuff, int unit, int track, int first, int sectors, int *status)
{
    USLOSS_Sysargs sysArg;

    CHECKMODE;
    sysArg.number = SYS_DISKWRITE;
    sysArg.arg1 = dbuff;
    sysArg.arg2 = (void *)(long) sectors;
    sysArg.arg3 = (void *)(long) track;
    sysArg.arg4 = (void *)(long) first;
    sysArg.arg5 = (void *)(long) unit;

    USLOSS_Syscall(&sysArg);

    *status = (long) sysArg.arg1;
    return (int)(long) sysArg.arg4;    
}

/*
 *  Routine:  DiskSize
 *
 *  Description: Returns information about the size of the disk.
 *
 *  Arguments:  int unit -- the unit number of the disk
 *              int *sector -- the pointer to the sector value
 *              int *track -- the pointer to the track value
 *              int *disk -- the pointer to the disk value
 *
 *  Return Value: 0 means success, -1 means error occurs 
 *
 */
int DiskSize (int unit, int *sector, int *track, int *disk)
{
    USLOSS_Sysargs sysArg;

    CHECKMODE;
    sysArg.number = SYS_DISKSIZE;
    sysArg.arg1 = (void *)(long) unit;

    USLOSS_Syscall(&sysArg);

    *sector = (long) sysArg.arg1;
    *track = (long) sysArg.arg2;
    *disk = (long) sysArg.arg3;
    return (int)(long) sysArg.arg4;
}

/*
 *  Routine:  TermRead
 *
 *  Description: Read a line from a terminal
 *
 *  Arguments:  char *buff -- address of the user’s line buffer.
 *              int bsize -- buffer size.
 *              int unit_id -- the unit number of the terminal from which to read.              
 *              int *nread -- maximum size of the buffer to read.
 *
 *  Return Value: 0 means success, -1 means error occurs 
 *
 */
int TermRead (char *buff, int bsize, int unit_id, int *nread)
{
    USLOSS_Sysargs sysArg;

    CHECKMODE;
    sysArg.number = SYS_TERMREAD;
    sysArg.arg1 = buff;
    sysArg.arg2 = (void *)(long) bsize;
    sysArg.arg3 = (void *)(long) unit_id;

    USLOSS_Syscall(&sysArg);

    *nread = (long) sysArg.arg2;
    return (int)(long) sysArg.arg4;
}

/*
 *  Routine:  TermWrite
 *
 *  Description: Write a line to a terminal
 *
 *  Arguments:  char *buff -- address of the user’s line buffer.
 *              int bsize -- buffer size.
 *              int unit_id -- the unit number of the terminal to which to write.
 *              int *nwrite -- number of characters to write.
 *
 *  Return Value: 0 means success, -1 means error occurs 
 *
 */
int TermWrite(char *buff, int bsize, int unit_id, int *nwrite)
{
    USLOSS_Sysargs sysArg;

    CHECKMODE;
    sysArg.number = SYS_TERMWRITE;
    sysArg.arg1 = buff;
    sysArg.arg2 = (void *)(long) bsize;
    sysArg.arg3 = (void *)(long) unit_id;

    USLOSS_Syscall(&sysArg);

    *nwrite = (long) sysArg.arg2;
    return (long) sysArg.arg4;
}


/* end libuser.c */
