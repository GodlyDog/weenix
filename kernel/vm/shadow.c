#include "vm/shadow.h"
#include "mm/page.h"
#include "mm/pframe.h"
#include "mm/slab.h"
#include "util/debug.h"
#include "util/string.h"

#define SHADOW_SINGLETON_THRESHOLD 5

typedef struct mobj_shadow
{
    // the mobj parts of this shadow object
    mobj_t mobj;
    // a reference to the mobj that is the data source for this shadow object
    // This should be a reference to a shadow object of some ancestor process.
    // This is used to traverse the shadow object chain.
    mobj_t *shadowed;
    // a reference to the mobj at the bottom of this shadow object's chain
    // this should NEVER be a shadow object (i.e. it should have some type other
    // than MOBJ_SHADOW)
    mobj_t *bottom_mobj;
} mobj_shadow_t;

#define MOBJ_TO_SO(o) CONTAINER_OF(o, mobj_shadow_t, mobj)

static slab_allocator_t *shadow_allocator;

static long shadow_get_pframe(mobj_t *o, size_t pagenum, long forwrite,
                              pframe_t **pfp);
static long shadow_fill_pframe(mobj_t *o, pframe_t *pf);
static long shadow_flush_pframe(mobj_t *o, pframe_t *pf);
static void shadow_destructor(mobj_t *o);

static mobj_ops_t shadow_mobj_ops = {.get_pframe = shadow_get_pframe,
                                     .fill_pframe = shadow_fill_pframe,
                                     .flush_pframe = shadow_flush_pframe,
                                     .destructor = shadow_destructor};

/*
 * Initialize shadow_allocator using the slab allocator.
 */
void shadow_init()
{
    shadow_allocator = slab_allocator_create("shadow", sizeof(mobj_shadow_t));
    KASSERT(shadow_allocator);
}

/*
 * Create a shadow object that shadows the given mobj.
 *
 * Return a new, LOCKED shadow object on success, or NULL upon failure.
 *
 * Hints:
 *  1) Create and initialize a mobj_shadow_t based on the given mobj.
 *  2) Set up the bottom object of the shadow chain, which could have two cases:
 *     a) Either shadowed is a shadow object, and you can use its bottom_mobj
 *     b) Or shadowed is not a shadow object, in which case it is the bottom 
 *        object of this chain.
 * 
 *  Make sure to manage the refcounts correctly.
 */
mobj_t *shadow_create(mobj_t *shadowed)
{
    mobj_shadow_t* shadow = slab_obj_alloc(shadow_allocator);
    if (!shadow) {
        return NULL;
    }
    if (shadowed->mo_type == MOBJ_SHADOW) {
        mobj_shadow_t* shadowed_shadow = MOBJ_TO_SO(shadowed);
        shadow->bottom_mobj = shadowed_shadow->bottom_mobj;
    } else {
        shadow->bottom_mobj = shadowed;
    }
    shadow->shadowed = shadowed;
    mobj_init(&shadow->mobj, MOBJ_SHADOW, &shadow_mobj_ops);
    mobj_ref(shadow->shadowed);
    mobj_ref(shadow->bottom_mobj);
    mobj_lock(&shadow->mobj);
    KASSERT(shadow->bottom_mobj->mo_type != MOBJ_SHADOW);
    KASSERT(shadow->mobj.mo_refcount == 1);
    return &shadow->mobj;
}

/*
 * Given a shadow object o, collapse its shadow chain as far as you can.
 *
 * Hints:
 *  1) You can only collapse if the shadowed object is a shadow object.
 *  2) When collapsing, you must manually migrate pframes from o's shadowed
 *     object to o, checking to see if a copy doesn't already exist in o.
 *  3) Be careful with refcounting! In particular, when you put away o's
 *     shadowed object, its refcount should drop to 0, initiating its
 *     destruction (shadow_destructor).
 *  4) As a reminder, any refcounting done in shadow_collapse() must play nice
 *     with any refcounting done in shadow_destructor().
 *  5) Pay attention to mobj and pframe locking.
 */
void shadow_collapse(mobj_t *o)
{
    mobj_shadow_t* shadow = MOBJ_TO_SO(o);
    mobj_t* current = shadow->shadowed;
    while (current != NULL && current->mo_type == MOBJ_SHADOW) {
        list_iterate(&current->mo_pframes, frame, pframe_t, pf_link) {
            pframe_t* found = NULL;
            mobj_lock(o);
            mobj_find_pframe(o, frame->pf_pagenum, &found);
            mobj_unlock(o);
            list_remove(&frame->pf_link);
            if (!found) {
                list_insert_tail(&o->mo_pframes, &frame->pf_link);
            } else {
                pframe_release(&found);
            }
        }
        shadow = MOBJ_TO_SO(current);
        KASSERT(current->mo_refcount == 1);
        mobj_put(&current);
        current = shadow->shadowed;
    }
    shadow = MOBJ_TO_SO(o);
    shadow->shadowed = shadow->bottom_mobj;
}

/*
 * Obtain the desired pframe from the given mobj, traversing its shadow chain if
 * necessary. This is where copy-on-write logic happens!
 *
 * Arguments: 
 *  o        - The object from which to obtain a pframe
 *  pagenum  - Number of the desired page relative to the object
 *  forwrite - Set if the caller wants to write to the pframe's data, clear if
 *             only reading
 *  pfp      - Upon success, pfp should point to the desired pframe.
 *
 * Return 0 on success, or:
 *  - Propagate errors from mobj_default_get_pframe() and mobj_get_pframe()
 *
 * Hints:
 *  1) If forwrite is set, use mobj_default_get_pframe().
 *  2) If forwrite is clear, check if o already contains the desired frame.
 *     a) If not, iterate through the shadow chain to find the nearest shadow
 *        mobj that has the frame. Do not recurse! If the shadow chain is long,
 *        you will cause a kernel buffer overflow (e.g. from forkbomb).
 *     b) If no shadow objects have the page, call mobj_get_pframe() to get the
 *        page from the bottom object and return what it returns.
 * 
 *  Pay attention to pframe locking.
 */
static long shadow_get_pframe(mobj_t *o, size_t pagenum, long forwrite,
                              pframe_t **pfp)
{
    mobj_shadow_t* shadow = MOBJ_TO_SO(o);
    KASSERT(shadow->bottom_mobj->mo_type != MOBJ_SHADOW);
    KASSERT(shadow->shadowed != o);
    if (forwrite) {
        kmutex_lock(&shadow->bottom_mobj->mo_mutex);
        long status = mobj_default_get_pframe(shadow->bottom_mobj, pagenum, 1, pfp); // QUESTION: Should I be calling this on the bottom mobj?
        kmutex_unlock(&shadow->bottom_mobj->mo_mutex);
        return status;
    } else {
        mobj_t* current = shadow->shadowed;
        while (current != NULL && current->mo_type == MOBJ_SHADOW) {
            mobj_lock(current);
            mobj_find_pframe(current, pagenum, pfp);
            mobj_unlock(current);
            if (*pfp) {
                return 0;
            }
            shadow = MOBJ_TO_SO(current);
            KASSERT(shadow->shadowed != current);
            current = shadow->shadowed;
        }
        mobj_lock(current);
        long status = mobj_get_pframe(current, pagenum, 0, pfp);
        mobj_unlock(current);
        return status;
    }
}

/*
 * Use the given mobj's shadow chain to fill the given pframe.
 *
 * Return 0 on success, or:
 *  - Propagate errors from mobj_get_pframe()
 *
 * Hints:
 *  1) Explore mobj_default_get_pframe(), which calls mobj_create_pframe(), to
 *     understand what state pf is in when this function is called, and how you
 *     can use it.
 *  2) As you can see above, shadow_get_pframe would call
 *     mobj_default_get_pframe (when the forwrite is set), which would 
 *     create and then fill the pframe (shadow_fill_pframe is called).
 *  3) Traverse the shadow chain for a copy of the frame, starting at the given
 *     mobj's shadowed object. You can use mobj_find_pframe to look for the 
 *     page frame. pay attention to locking/unlocking, and be sure not to 
 *     recurse when traversing.
 *  4) If none of the shadow objects have a copy of the frame, use
 *     mobj_get_pframe on the bottom object to get it.
 *  5) After obtaining the desired frame, simply copy its contents into pf.
 */
static long shadow_fill_pframe(mobj_t *o, pframe_t *pf)
{
    mobj_shadow_t* shadow = MOBJ_TO_SO(o);
    mobj_t* current = shadow->shadowed;
    pframe_t* found;
    while (current != NULL && current->mo_type == MOBJ_SHADOW) {
        mobj_lock(current);
        mobj_find_pframe(current, pf->pf_pagenum, &found);
        mobj_unlock(current);
        if (found) {
            memcpy(pf->pf_addr, found->pf_addr, PAGE_SIZE);
            pframe_release(&found);
            return 0;
        }
        shadow = MOBJ_TO_SO(current);
        current = shadow->shadowed;
    }
    long owns = kmutex_owns_mutex(&current->mo_mutex);
    if (!owns) { // SPAGHETTI
        mobj_lock(current);
    }
    long status = mobj_get_pframe(current, pf->pf_pagenum, 0, &found);
    if (!owns) { // MORE SPAGHETTI
        mobj_unlock(current);
    }
    if (status == 0) {
        memcpy(pf->pf_addr, found->pf_addr, PAGE_SIZE);
        pframe_release(&found);
    }
    return status;
}

/*
 * Flush a shadow object's pframe to disk.
 *
 * Return 0 on success.
 *
 * Hint:
 *  - Are shadow objects backed to disk? Do you actually need to do anything
 *    here?
 */
static long shadow_flush_pframe(mobj_t *o, pframe_t *pf)
{
    return 0;
}

/*
 * Clean up all resources associated with mobj o.
 *
 * Hints:
 *  - Check out mobj_put() to understand how this function gets called.
 *
 *  1) Call mobj_default_destructor() to flush o's pframes.
 *  2) Put the shadow and bottom_mobj members of the shadow object.
 *  3) Free the mobj_shadow_t.
 */
static void shadow_destructor(mobj_t *o)
{
    mobj_shadow_t* shadow = MOBJ_TO_SO(o);
    mobj_default_destructor(o);
    mobj_put(&shadow->shadowed);
    mobj_put(&shadow->bottom_mobj);
    slab_obj_free(shadow_allocator, shadow);
}
