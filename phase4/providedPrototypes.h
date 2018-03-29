/*
 * Function prototypes from Patrick's phase3 solution. These can be called
 * when in *kernel* mode to get access to phase3 functionality.
 */


#ifndef PROVIDED_PROTOTYPES_H

#define PROVIDED_PROTOTYPES_H

extern int  spawnReal(char *name, int (*func)(char *), char *arg,
                       int stack_size, int priority);
extern int  waitReal(int *status);
extern void terminateReal(int exit_code);
extern int  semcreateReal(int init_value);
extern int  sempReal(int semaphore);
extern int  semvReal(int semaphore);
extern int  semfreeReal(int semaphore);
extern int  gettimeofdayReal(long *time);
extern int  cputimeReal(long *time);
extern int  getPID_real(long *pid);

#endif  /* PROVIDED_PROTOTYPES_H */
