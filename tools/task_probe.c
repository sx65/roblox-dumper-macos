#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>

static int try_task_for_pid(pid_t pid) {
    mach_port_t task = MACH_PORT_NULL;
    kern_return_t kr = task_for_pid(mach_task_self(), pid, &task);
    printf("task_for_pid: kr=%d (%s) task=0x%x\n", kr, mach_error_string(kr), task);
    return kr == KERN_SUCCESS;
}

static int try_processor_set_tasks(pid_t pid) {
    host_t host = mach_host_self();
    processor_set_name_t pset = PROCESSOR_SET_NULL;
    processor_set_t default_set = PROCESSOR_SET_NULL;
    task_t* tasks = NULL;
    mach_msg_type_number_t task_count = 0;

    kern_return_t kr = host_processor_set(host, PROCESSOR_SET_NULL, &pset);
    if (kr != KERN_SUCCESS) {
        printf("host_processor_set failed: %s\n", mach_error_string(kr));
        return 0;
    }

    kr = processor_set_default(pset, &default_set);
    if (kr != KERN_SUCCESS) {
        printf("processor_set_default failed: %s\n", mach_error_string(kr));
        return 0;
    }

    kr = processor_set_tasks(default_set, &tasks, &task_count);
    if (kr != KERN_SUCCESS) {
        printf("processor_set_tasks failed: %s\n", mach_error_string(kr));
        return 0;
    }

    printf("processor_set_tasks returned %u tasks\n", task_count);
    for (mach_msg_type_number_t i = 0; i < task_count; i++) {
        int task_pid = pid_for_task(tasks[i]);
        if (task_pid == pid) {
            printf("matched pid %d via processor_set_tasks\n", pid);
            return 1;
        }
    }

    printf("pid %d not found in processor_set_tasks\n", pid);
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <pid>\n", argv[0]);
        return 1;
    }

    pid_t pid = (pid_t)atoi(argv[1]);
    try_task_for_pid(pid);
    try_processor_set_tasks(pid);
    return 0;
}
