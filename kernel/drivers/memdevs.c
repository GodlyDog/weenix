#include "errno.h"
#include "globals.h"

#include "util/debug.h"
#include "util/string.h"

#include "mm/kmalloc.h"
#include "mm/mobj.h"

#include "drivers/chardev.h"

#include "vm/anon.h"

#include "fs/vnode.h"

static ssize_t null_read(chardev_t *dev, size_t pos, void *buf, size_t count);

static ssize_t null_write(chardev_t *dev, size_t pos, const void *buf,
                          size_t count);

static ssize_t zero_read(chardev_t *dev, size_t pos, void *buf, size_t count);

static long zero_mmap(vnode_t *file, mobj_t **ret);

chardev_ops_t null_dev_ops = {.read = null_read,
                              .write = null_write,
                              .mmap = NULL,
                              .fill_pframe = NULL,
                              .flush_pframe = NULL};

chardev_ops_t zero_dev_ops = {.read = zero_read,
                              .write = null_write,
                              .mmap = zero_mmap,
                              .fill_pframe = NULL,
                              .flush_pframe = NULL};

/**
 * The char device code needs to know about these mem devices, so create
 * chardev_t's for null and zero, fill them in, and register them.
 *
 * Use kmalloc, MEM_NULL_DEVID, MEM_ZERO_DEVID, and chardev_register.
 * See dev.h for device ids to use with MKDEVID.
 */
void memdevs_init()
{
    NOT_YET_IMPLEMENTED("DRIVERS: memdevs_init");
}

/**
 * Reads a given number of bytes from the null device into a
 * buffer. Any read performed on the null device should read 0 bytes.
 *
 * @param  dev   the null device
 * @param  pos   the offset to read from; should be ignored
 * @param  buf   the buffer to read into
 * @param  count the maximum number of bytes to read
 * @return       the number of bytes read, which should be 0
 */
static ssize_t null_read(chardev_t *dev, size_t pos, void *buf, size_t count)
{
    NOT_YET_IMPLEMENTED("DRIVERS: null_read");
    return -ENOMEM;
}

/**
 * Writes a given number of bytes to the null device from a
 * buffer. Writing to the null device should _ALWAYS_ be successful
 * and write the maximum number of bytes.
 *
 * @param  dev   the null device
 * @param  pos   offset the offset to write to; should be ignored
 * @param  buf   buffer to read from
 * @param  count the maximum number of bytes to write
 * @return       the number of bytes written, which should be `count`
 */
static ssize_t null_write(chardev_t *dev, size_t pos, const void *buf,
                          size_t count)
{
    NOT_YET_IMPLEMENTED("DRIVERS: null_write");
    return -ENOMEM;
}

/**
 * Reads a given number of bytes from the zero device into a
 * buffer. Any read from the zero device should be a series of zeros.
 *
 * @param  dev   the zero device
 * @param  pos   the offset to start reading from; should be ignored
 * @param  buf   the buffer to write to
 * @param  count the maximum number of bytes to read
 * @return       the number of bytes read. Hint: should always read the maximum
 *               number of bytes
 */
static ssize_t zero_read(chardev_t *dev, size_t pos, void *buf, size_t count)
{
    NOT_YET_IMPLEMENTED("DRIVERS: zero_read");
    return 0;
}

/**
 * Unlike in s5fs_mmap(), you can't necessarily use the file's underlying mobj.
 * Instead, you should simply provide an anonymous object to ret.
 */
static long zero_mmap(vnode_t *file, mobj_t **ret)
{
    NOT_YET_IMPLEMENTED("VM: zero_mmap");
    return -1;
}
