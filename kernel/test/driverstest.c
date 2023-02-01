#include "errno.h"
#include "globals.h"

#include "test/usertest.h"
#include "test/proctest.h"

#include "util/debug.h"
#include "util/printf.h"
#include "util/string.h"

#include "proc/proc.h"
#include "proc/kthread.h"
#include "proc/sched.h"

#include "drivers/tty/tty.h"
#include "drivers/dev.h"
#include "drivers/blockdev.h"
#include "drivers/keyboard.h"

#define TEST_STR_1 "hello\n"
#define TEST_STR_2 "different string\n"
#define TEST_STR_3 "test"
#define TEST_BUF_SZ 10
#define NUM_PROCS 3
#define BLOCK_NUM 0

/*
    Tests inputting a character and a newline character 
*/
long test_basic_line_discipline() { 
    chardev_t* cd = chardev_lookup(MKDEVID(TTY_MAJOR, 0)); 
    tty_t* tty = cd_to_tty(cd); 
    ldisc_t* ldisc = &tty->tty_ldisc; 
    ldisc_key_pressed(ldisc, 't'); 

    test_assert(ldisc->ldisc_buffer[ldisc->ldisc_tail] == 't', 
                "character not inputted into buffer correctly"); 
    test_assert(ldisc->ldisc_head != ldisc->ldisc_cooked && ldisc->ldisc_tail != ldisc->ldisc_head, 
                "pointers are updated correctly");

    size_t previous_head_val = ldisc->ldisc_head; 
    ldisc_key_pressed(ldisc, '\n'); 
    test_assert(ldisc->ldisc_head == previous_head_val + 1, 
                "ldisc_head should have been incremented past newline character");
    test_assert(ldisc->ldisc_cooked == ldisc->ldisc_head, 
                "ldisc_cooked should be equal to ldisc_head"); 

    // reset line discipline for other tests before returning 
    ldisc->ldisc_head = ldisc->ldisc_cooked = ldisc->ldisc_tail = 0; 
    return 0; 
}

long driverstest_main(long arg1, void* arg2)
{
    dbg(DBG_TEST, "\nStarting Drivers tests\n");
    test_init();

    test_basic_line_discipline();

    // Add your tests here!

    test_fini();
    return 0;
}