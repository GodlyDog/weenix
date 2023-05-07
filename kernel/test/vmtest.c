#include "errno.h"
#include "globals.h"

#include "test/usertest.h"
#include "test/proctest.h"

#include "util/debug.h"
#include "util/printf.h"
#include "util/string.h"

#include "mm/mm.h"
#include "mm/page.h"
#include "mm/slab.h"
#include "mm/kmalloc.h"
#include "vm/vmmap.h"
#include "fs/open.h"
#include "fs/file.h"
#include "fs/fcntl.h"
#include "mm/mman.h"
#include "fs/vnode.h"

long test_vmmap() {
    vmmap_t *map = curproc->p_vmmap;

    // Make sure we start out cleanly
    KASSERT(vmmap_is_range_empty(map, ADDR_TO_PN(USER_MEM_LOW), ADDR_TO_PN(USER_MEM_HIGH - USER_MEM_LOW)));

    // Go through the address space, make sure we find nothing
    for (size_t i = USER_MEM_LOW; i < ADDR_TO_PN(USER_MEM_HIGH); i += PAGE_SIZE) {
        KASSERT(!vmmap_lookup(map, i));
    }
    
    // You can probably change this.
    size_t num_vmareas = 5;
    // Probably shouldn't change this to anything that's not a power of two.
    size_t num_pages_per_vmarea = 16;

    size_t prev_start = ADDR_TO_PN(USER_MEM_HIGH);
    for (size_t i = 0; i < num_vmareas; i++) {
        ssize_t start = vmmap_find_range(map, num_pages_per_vmarea, VMMAP_DIR_HILO);
        test_assert(start + num_pages_per_vmarea == prev_start, "Incorrect return value from vmmap_find_range");
        
        vmarea_t *vma = kmalloc(sizeof(vmarea_t));
        KASSERT(vma && "Unable to alloc the vmarea");
        memset(vma, 0, sizeof(vmarea_t));

        vma->vma_start = start;
        vma->vma_end = start + num_pages_per_vmarea;
        vmmap_insert(map, vma);

        prev_start = start;
    }

    // Now, our address space should look like:
    // EMPTY EMPTY EMPTY [  ][  ][  ][  ][  ]
    // ^LP
    //                                      ^HP
    //                   ^section_start 
    // HP --> the highest possible userland page number
    // LP --> the lowest possible userland page number 
    // section start --> HP - (num_vmareas * num_pages_per_vmarea) 
    
    list_iterate(&map->vmm_list, vma, vmarea_t, vma_plink) {
        list_remove(&vma->vma_plink); 
        kfree(vma);
    }

    ssize_t start = vmmap_find_range(map, 16, VMMAP_DIR_LOHI);
    test_assert(start == ADDR_TO_PN(USER_MEM_LOW), "Range is wonky on the lohi portion");
    ssize_t other_start = vmmap_find_range(map, 16, VMMAP_DIR_HILO);
    test_assert(other_start == ADDR_TO_PN(USER_MEM_HIGH) - 16, "Range is wonky on the hilo portion");
    long fd = do_open("Hello", O_RDONLY | O_CREAT);
    file_t* file = fget(fd);
    size_t off = PAGE_SIZE;
    vmarea_t* area;
    vmmap_map(curproc->p_vmmap, file->f_vnode, start, 16, PROT_READ, MAP_FIXED, off, VMMAP_DIR_HILO, &area);
    test_assert(area->vma_start == (size_t) start, "Start is wrong");
    test_assert(area->vma_end == (size_t) (start + 16), "End is wrong");
    test_assert(area->vma_off == 1, "Offset is wrong");
    test_assert(area->vma_prot == PROT_READ, "Prot is wrong");
    test_assert(area->vma_flags == MAP_FIXED, "Flags are wrong");
    test_assert(area->vma_vmmap == curproc->p_vmmap, "Map is wrong");
    test_assert(area->vma_obj->mo_type == file->f_vnode->vn_mobj.mo_type, "Obj is wrong");
    list_iterate(&map->vmm_list, vma, vmarea_t, vma_plink) {
        test_assert(vma == area, "Not the same area as created");
    }
    vmmap_remove(map, start, 16);
    test_assert(list_empty(&map->vmm_list), "List not empty");
    vmmap_map(curproc->p_vmmap, file->f_vnode, start, 32, PROT_READ, MAP_FIXED, off, VMMAP_DIR_HILO, &area);
    vmmap_remove(map, start, 16);
    list_iterate(&map->vmm_list, vma, vmarea_t, vma_plink) {
        test_assert(vma->vma_start == (size_t) (start + 16), "Start is wrong");
        test_assert(vma->vma_end == area->vma_end, "End is wrong");
        test_assert(vma->vma_off == 17, "Offset is wrong");
    }
    vmmap_remove(map, start + 16, 16);
    test_assert(list_empty(&map->vmm_list), "List not empty");
    vmmap_map(curproc->p_vmmap, file->f_vnode, start, 32, PROT_READ, MAP_FIXED, off, VMMAP_DIR_HILO, &area);
    vmmap_remove(map, start + 16, 16);
    list_iterate(&map->vmm_list, vma, vmarea_t, vma_plink) {
        test_assert(vma->vma_start == area->vma_start, "Start is wrong");
        test_assert(vma->vma_end == (size_t) (start + 16), "End is wrong");
        test_assert(vma->vma_off == area->vma_off, "Offset is wrong");
    }
    test_assert(!vmmap_is_range_empty(map, start, 16), "Range not empty");
    test_assert(!vmmap_is_range_empty(map, start, start + 32), "Range is empty");
    vmmap_remove(map, start, 16);
    test_assert(vmmap_is_range_empty(map, start, 16), "Range not empty");
    test_assert(list_empty(&map->vmm_list), "List not empty");
    vmmap_map(curproc->p_vmmap, file->f_vnode, start, 32, PROT_READ, MAP_FIXED, off, VMMAP_DIR_HILO, &area);
    vmmap_remove(map, start + 8, 16);
    size_t count = 0;
    list_iterate(&map->vmm_list, vma, vmarea_t, vma_plink) {
        count += 1;
        if (count == 1) {
            test_assert(vma->vma_start == area->vma_start, "Start is wrong");
            test_assert(vma->vma_end == (size_t) (start + 8), "End is wrong");
            test_assert(vma->vma_off == area->vma_off, "Offset is wrong");
        } else {
            test_assert(vma->vma_start == (size_t) (start + 24), "Start is wrong");
            test_assert(vma->vma_end == (size_t) (start + 32), "End is wrong");
            test_assert(vma->vma_off == area->vma_off + 24, "Offset is wrong");
        }
    }
    test_assert(count == 2, "Not the expected number of vmareas");
    vmmap_remove(map, start, 32);
    test_assert(vmmap_is_range_empty(map, start, 32), "Vmarea not removed");
    vmmap_map(curproc->p_vmmap, file->f_vnode, start, 32, PROT_READ, MAP_FIXED, 0, VMMAP_DIR_HILO, &area);
    const char* buf = "This should be readable";
    count = strlen(buf);
    file->f_vnode->vn_len = PAGE_SIZE * 32;
    long status = vmmap_write(map, PN_TO_ADDR(start), buf, count);
    test_assert(status == 0, "Write failed");
    char* receive = "";
    status = vmmap_read(map, PN_TO_ADDR(start), receive, count);
    test_assert(status == 0, "Read failed");
    test_assert(!strncmp(buf, receive, count), "Did not read correctly");
    
    return 0; 
}

long vmtest_main(long arg1, void* arg2) {
    test_init(); 
    test_vmmap(); 

    // Write your own tests here!

    test_fini(); 
    return 0; 
}
