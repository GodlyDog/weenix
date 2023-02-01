#include "errno.h"
#include "globals.h"

#include "test/proctest.h"
#include "test/usertest.h"

#include "util/debug.h"
#include "util/printf.h"
#include "util/string.h"

#include "proc/kthread.h"
#include "proc/proc.h"
#include "proc/sched.h"

/*
 * Set up a testing function for the process to execute. 
*/
void *test_func(long arg1, void *arg2)
{
    proc_t *proc_as_arg = (proc_t *)arg2;
    test_assert(arg1 == proc_as_arg->p_pid, "Arguments are not set up correctly");
    test_assert(proc_as_arg->p_state == PROC_RUNNING, "Process state is not running");
    test_assert(list_empty(&proc_as_arg->p_children), "There should be no child processes");
    return NULL;
}

void test_termination()
{
    int num_procs_created = 0;
    proc_t *new_proc1 = proc_create("proc test 1");
    kthread_t *new_kthread1 = kthread_create(new_proc1, test_func, 2, new_proc1);
    num_procs_created++;
    sched_make_runnable(new_kthread1);

    int count = 0;
    int status;
    while (do_waitpid(-1, &status, 0) != -ECHILD)
    {
        test_assert(status == 0, "Returned status not set correctly");
        count++;
    }
    test_assert(count == num_procs_created,
                "Expected: %d, Actual: %d number of processes have been cleaned up\n", num_procs_created, count);
}

long proctest_main(long arg1, void *arg2)
{
    dbg(DBG_TEST, "\nStarting Procs tests\n");
    test_init();
    test_termination();

    // Add more tests here!

    test_fini();
    return 0;
}