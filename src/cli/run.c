#include "cli.h"

#include <screw/crash.h>
#include <screw/handle.h>
#include <screw/loader.h>
#include <screw/log.h>
#include <screw/path.h>
#include <screw/process.h>
#include <screw/teb.h>
#include <screw/thread.h>

typedef int WINAPI (*entry_point)(void *peb);

int cli_run(int argc, char **argv)
{
    crash_install();
    path_init();
    handles_init();
    process_init(argc, argv);
    thread_attach_main();

    struct module *exe = module_load_main(process_exe_host());
    if (exe->is_dll)
        die("%s is a DLL, not a program", argv[0]);

    struct peb *peb = peb_get();
    peb->image_base = exe->base;
    modules_attach();

    entry_point entry = (entry_point)(exe->base + exe->nt->opt.entry);
    trace("entering %s at %p", exe->name, (void *)entry);
    process_exit(entry(peb));
}
