#include "kernel.hh"
#include "k-apic.hh"
#include "k-vmiter.hh"
#include <atomic>

// kernel.cc
//
//    This is the kernel.


// INITIAL PHYSICAL MEMORY LAYOUT
//
//  +-------------- Base Memory --------------+
//  v                                         v
// +-----+--------------------+----------------+--------------------+---------/
// |     | Kernel      Kernel |       :    I/O | App 1        App 1 | App 2
// |     | Code + Data  Stack |  ...  : Memory | Code + Data  Stack | Code ...
// +-----+--------------------+----------------+--------------------+---------/
// 0  0x40000              0x80000 0xA0000 0x100000             0x140000
//                                             ^
//                                             | \___ PROC_SIZE ___/
//                                      PROC_START_ADDR

#define PROC_SIZE 0x40000       // initial state only

proc ptable[NPROC];             // array of process descriptors
                                // Note that `ptable[0]` is never used.
proc* current;                  // pointer to currently executing proc

#define HZ 100                  // timer interrupt frequency (interrupts/sec)
static std::atomic<unsigned long> ticks; // # timer interrupts so far


// Memory state
//    Information about physical page with address `pa` is stored in
//    `pages[pa / PAGESIZE]`. In the handout code, each `pages` entry
//    holds an `refcount` member, which is 0 for free pages.
//    You can change this as you see fit.

pageinfo pages[NPAGES];


[[noreturn]] void schedule();
[[noreturn]] void run(proc* p);
void exception(regstate* regs);
uintptr_t syscall(regstate* regs);
void memshow();


static void process_setup(pid_t pid, const char* program_name);

// EXPLORE 1
// kernel(command)
//    Initialize the hardware and processes and start running. char* is a String
//    in c++/c. What is the purpose of the command String parameter?
void kernel(const char* command) {
    // initialize hardware
    init_hardware(); //lot's happens here, you can look at this if you'd like 
    log_printf("Starting WeensyOS\n"); ///EXPLORE 2 print to a log file called log.txt

    ticks = 1;
    init_timer(HZ); // EXPLORE 3 initializes the timer that calls an interrupt
                    // new processes are run every time the timer calls the interrupt
                    // Why is it necessary to have an interrupt?

    // clear screen
    console_clear();

    //EXPLORE 4 (re-)initialize kernel page table
    // What does vmiter do?
    for (vmiter page_i(kernel_pagetable); page_i.va() < MEMSIZE_PHYSICAL; page_i += PAGESIZE) {
        //EXPLORE 5: by using the map method, we can set the permissions for an virtual
        // address' page. Explain, in a sentence or two, what mapping is. 
        if (page_i.va() != 0) {
            page_i.map(page_i.va(), PTE_P | PTE_W | PTE_U); //EXLPORE 6: look for the user permissions 
                                                    //and their meaning in x86-64.h
                                                    //These permissions are bit flags, what do they stand for?

            /***TODO 1: Each User process should only have permission to 
                pages with address greater or equal to the PROC's start address
                hint: use PROC_START_ADDR***/
            /***TODO 2: Make sure that User processes also have permission to use
                the Console's address, otherwise they won't be able to print anything 
                to the screen ***/
        } else {
            // nullptr is inaccessible even to the kernel
            page_i.map(page_i.va(), 0); //this null pointer's pagetable
        }
    }

    //EXPLORE 7: Once the kernel has created it's own page table with permissions
    // it can then call for processes to be set up and started, What is happening in the last 13 lines of this function?

    // set up process descriptors
    for (pid_t i = 0; i < NPROC; i++) { 
        ptable[i].pid = i;
        ptable[i].state = P_FREE;
    }
    if (command && program_loader(command).present()) { //This is only applicable if you're doing the 
                                                        //extra credit
        process_setup(1, command);
    } else {
        process_setup(1, "allocator");  //EXLPORE 8: The processes are started here
        process_setup(2, "allocator2"); // you will work on the process_setup function in
        process_setup(3, "allocator3"); // in future steps. 
        process_setup(4, "allocator4");
    }

    // Switch to the first process using run()
    run(&ptable[1]);
}

// EXPLORE 14:
// kalloc(sz)
//    Kernel memory allocator. Allocates `sz` contiguous bytes and
//    returns a pointer to the allocated memory, or `nullptr` on failure.
//
//    The returned memory is initialized to 0xCC, which corresponds to
//    the x86 instruction `int3` (this may help you debug). You'll
//    probably want to reset it to something more useful.
//
//    On WeensyOS, `kalloc` is a page-based allocator: if `sz > PAGESIZE`
//    the allocation fails; if `sz < PAGESIZE` it allocates a whole page
//    anyway.
//
//    The handout code returns the next allocatable free page it can find.

static uintptr_t next_alloc_pa;

void* kalloc(size_t sz) {
    if (sz > PAGESIZE) {
        return nullptr;
    }

    while (next_alloc_pa < MEMSIZE_PHYSICAL) {
        uintptr_t pa = next_alloc_pa;
        next_alloc_pa += PAGESIZE;

        //EXPLORE 15: What is kalloc doing  in this while loop?
        //what does it return?
        //what is pa and how is it different from va?
        if (allocatable_physical_address(pa)
            && !pages[pa / PAGESIZE].used()) {
            pages[pa / PAGESIZE].refcount = 1;
            memset((void*) pa, 0, PAGESIZE);
            return (void*) pa;
        }
    }
    return nullptr;
}


// kfree(kptr)
//    Free `kptr`, which must have been previously returned by `kalloc`.
//    If `kptr == nullptr` does nothing.

void kfree(void* kptr) {
    (void) kptr;
    assert(false /* your code here */);
}

// get_new_pagetable()
// here we return the address to the start of the pagetable.
// a pointer in c++ is just an address in memory 
// pointers are defined with *
x86_64_pagetable* get_new_pagetable()
{
    x86_64_pagetable* new_table = (x86_64_pagetable*) kalloc(PAGESIZE);
    return new_table;
}


// process_setup(pid, program_name)
//    Load application program `program_name` as process number `pid`.
//    This loads the application's code and data into memory, sets its
//    %rip and %rsp, gives it a stack page, and marks it as runnable.

void process_setup(pid_t pid, const char* program_name) {
    init_process(&ptable[pid], 0);

    // initialize process page table
    ptable[pid].pagetable = kernel_pagetable; //Explore 9: ptable holds all of the processes
                                              //here we mark each process' page table as the
                                              //kernel's pagetable :( Look at the proc struct in kernel.hh
                                              //Explain what makes up a process struct. 

                                          /***TODO 3: currently, processes are all being given the 
                                          * kernel_pagetable as their pagetable on line 185. Every process 
                                          * should havheir own page table. 
                                          * There is a function that will help you give the current process 
                                          * being setup a new pagetable. ***/e t

                                         /***TODO 4: This is where we need to give each process their own shelf. 
                                          * For the this process, calculate the code, data, start of heap, and stack addresses. 
                                          * Set the corresponding virtual memory blocks to have user permission PTE_PWU ***/

    // load the program
    program_loader loader(program_name); //Explore 10: We use this to load the programs instructions into 
                                         //memory, this goes along with Explore 11

                                         
    /***TODO 5: Each process has it's own stack, data instructions and heap. Each of these is currently being 
    allocated with a direct mapping. So virtual page X corresponds to virtual page X, which isn't the 
    most efficient. You wan't to map virtual page X to whatever is the next available page in physical
    memory. Which function will help with getting the next available memory? ***/

    //EXPLORE 16: this loop allocates and maps data and code instructions, how does it do that?
    for (loader.reset(); loader.present(); ++loader) {
        for (uintptr_t a = round_down(loader.va(), PAGESIZE);
             a < loader.va() + loader.size();
             a += PAGESIZE) {
            assert(!pages[a / PAGESIZE].used()); // check if page is used
            pages[a / PAGESIZE].refcount = 1; // if not used then, set it as used with reference count not 0
        }
    }

    // Explore 11: copy code instructions and data into place
    // each program has pages devoted solely to it's instructions and data
    // this loop copies those into memory
    for (loader.reset(); loader.present(); ++loader) {
        memset((void*) loader.va(), 0, loader.size());
        memcpy((void*) loader.va(), loader.data(), loader.data_size());
    }

    // mark entry point
    ptable[pid].regs.reg_rip = loader.entry();

    // Explore 12: allocate stack
    // each process will need to allocate it's own stack, the part of memory we
    // learned about in pset 2. The CPU and ALU will use this space in memory
    // for this process. In this pset, the last page for the process (highest address)
    // is the stack
    uintptr_t stack_addr = PROC_START_ADDR + PROC_SIZE * pid - PAGESIZE;
    assert(!pages[stack_addr / PAGESIZE].used()); 
    pages[stack_addr / PAGESIZE].refcount = 1;
    ptable[pid].regs.reg_rsp = stack_addr + PAGESIZE;

    //Explore 13: mark process as runnable
    //find out why we have to do this? Search for P_RUNNABLE in
    //other parts of the code
    ptable[pid].state = P_RUNNABLE;
}



// exception(regs)
//    Exception handler (for interrupts, traps, and faults).
//
//    The register values from exception time are stored in `regs`.
//    The processor responds to an exception by saving application state on
//    the kernel's stack, then jumping to kernel assembly code (in
//    k-exception.S). That code saves more registers on the kernel's stack,
//    then calls exception().
//
//    Note that hardware interrupts are disabled when the kernel is running.

void exception(regstate* regs) {
    // Copy the saved registers into the `current` process descriptor.
    current->regs = *regs;
    regs = &current->regs;

    // It can be useful to log events using `log_printf`.
    // Events logged this way are stored in the host's `log.txt` file.
    /* log_printf("proc %d: exception %d at rip %p\n",
                current->pid, regs->reg_intno, regs->reg_rip); */

    // Show the current cursor location and memory state
    // (unless this is a kernel fault).
    console_show_cursor(cursorpos);
    if (regs->reg_intno != INT_PF || (regs->reg_errcode & PFERR_USER)) {
        memshow();
    }

    // If Control-C was typed, exit the virtual machine.
    check_keyboard();


    // Actually handle the exception.
    switch (regs->reg_intno) {

    case INT_IRQ + IRQ_TIMER:
        ++ticks;
        lapicstate::get().ack();
        schedule();
        break;                  /* will not be reached */

    case INT_PF: {
        // Analyze faulting address and access type.
        uintptr_t addr = rdcr2();
        const char* operation = regs->reg_errcode & PFERR_WRITE
                ? "write" : "read";
        const char* problem = regs->reg_errcode & PFERR_PRESENT
                ? "protection problem" : "missing page";

        if (!(regs->reg_errcode & PFERR_USER)) {
            panic("Kernel page fault for %p (%s %s, rip=%p)!\n",
                  addr, operation, problem, regs->reg_rip);
        }
        console_printf(CPOS(24, 0), 0x0C00,
                       "Process %d page fault for %p (%s %s, rip=%p)!\n",
                       current->pid, addr, operation, problem, regs->reg_rip);
        current->state = P_BROKEN;
        break;
    }

    default:
        panic("Unexpected exception %d!\n", regs->reg_intno);

    }


    // Return to the current process (or run something else).
    if (current->state == P_RUNNABLE) {
        run(current);
    } else {
        schedule();
    }
}


// syscall(regs)
//    System call handler.
//
//    The register values from system call time are stored in `regs`.
//    The return value, if any, is returned to the user process in `%rax`.
//
//    Note that hardware interrupts are disabled when the kernel is running.

int syscall_page_alloc(uintptr_t addr);

uintptr_t syscall(regstate* regs) {
    // Copy the saved registers into the `current` process descriptor.
    current->regs = *regs;
    regs = &current->regs;

    // It can be useful to log events using `log_printf`.
    // Events logged this way are stored in the host's `log.txt` file.
    /* log_printf("proc %d: syscall %d at rip %p\n",
                  current->pid, regs->reg_rax, regs->reg_rip); */

    // Show the current cursor location and memory state
    // (unless this is a kernel fault).
    console_show_cursor(cursorpos);
    memshow();

    // If Control-C was typed, exit the virtual machine.
    check_keyboard();


    // Actually handle the exception.
    switch (regs->reg_rax) {

    case SYSCALL_PANIC:
        panic(nullptr);         // does not return

    case SYSCALL_GETPID:
        return current->pid;

    case SYSCALL_YIELD:
        current->regs.reg_rax = 0;
        schedule();             // does not return

    case SYSCALL_PAGE_ALLOC:
        return syscall_page_alloc(current->regs.reg_rdi);

    default:
        panic("Unexpected system call %ld!\n", regs->reg_rax);

    }

    panic("Should not get here!\n");
}


// syscall_page_alloc(addr)
//    Handles the SYSCALL_PAGE_ALLOC system call. This function
//    should implement the specification for `sys_page_alloc`
//    in `u-lib.hh`
int syscall_page_alloc(uintptr_t addr) { //EXPLORE 17: Who calls this function and why? How often does it get called?
                                         //What part of a process gets allocated here?
    //TODO 6: This method uses direct mapping, how could we use kalloc to have dynamic mapping?
    vmiter(current->pagetable, addr).map(addr, PTE_PWU);
    assert(!pages[addr / PAGESIZE].used());
    pages[addr / PAGESIZE].refcount = 1;
    memset((void*) addr, 0, PAGESIZE);
    return 0;
}


// schedule
//    Pick the next process to run and then run it.
//    If there are no runnable processes, spins forever.

void schedule() {
    pid_t pid = current->pid;
    for (unsigned spins = 1; true; ++spins) {
        pid = (pid + 1) % NPROC;
        if (ptable[pid].state == P_RUNNABLE) {
            run(&ptable[pid]);
        }

        // If Control-C was typed, exit the virtual machine.
        check_keyboard();

        // If spinning forever, show the memviewer.
        if (spins % (1 << 12) == 0) {
            memshow();
            log_printf("%u\n", spins);
        }
    }
}


// run(p)
//    Run process `p`. This involves setting `current = p` and calling
//    `exception_return` to restore its page table and registers.

void run(proc* p) {
    assert(p->state == P_RUNNABLE);
    current = p;

    // Check the process's current pagetable.
    check_pagetable(p->pagetable);

    // This function is defined in k-exception.S. It restores the process's
    // registers then jumps back to user mode.
    exception_return(p);

    // should never get here
    while (true) {
    }
}


// memshow()
//    Draw a picture of memory (physical and virtual) on the CGA console.
//    Switches to a new process's virtual memory map every 0.25 sec.
//    Uses `console_memviewer()`, a function defined in `k-memviewer.cc`.

void memshow() {
    static unsigned last_ticks = 0;
    static int showing = 0;

    // switch to a new process every 0.25 sec
    if (last_ticks == 0 || ticks - last_ticks >= HZ / 2) {
        last_ticks = ticks;
        showing = (showing + 1) % NPROC;
    }

    proc* p = nullptr;
    for (int search = 0; !p && search < NPROC; ++search) {
        if (ptable[showing].state != P_FREE
            && ptable[showing].pagetable) {
            p = &ptable[showing];
        } else {
            showing = (showing + 1) % NPROC;
        }
    }

    extern void console_memviewer(proc* vmp);
    console_memviewer(p);
}
