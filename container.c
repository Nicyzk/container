#define _GNU_SOURCE
#include <sched.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <string.h>
#include <fcntl.h>

#define CGROUP_FOLDER "/sys/fs/cgroup/pids/container/"
const int STACK_SIZE = 65536;

// Allocate stack for child task.
char* stack_memory() {
    char* stack = malloc(STACK_SIZE);
    if (!stack) {
        perror("malloc");
        EXIT_FAILURE;
    }
    return stack+STACK_SIZE; // stack grows downwards
}

void setup_variables() {
  clearenv();   // only child's env vars are cleared
  setenv("TERM", "xterm-256color", 0);
  setenv("PATH", "/bin/:/sbin/:usr/bin:/usr/sbin", 0);
}

void setup_root(const char* folder){
  chroot(folder);
  chdir("/");
}

int run(const char *name) {
    char *args[] = {(char *)name, (char*)0};
    return execvp(name, args);
}

int callback_run() { run("/bin/sh"); }

int clone_process(int (*func)()) {  // allows you to clone a child to exec a function and wait for the child to terminate
    char *stack = stack_memory();
    if (clone(func, stack, SIGCHLD, 0) == -1) {
        perror("clone_process");
        return EXIT_FAILURE;
    }
    wait(NULL);
    return EXIT_SUCCESS;
}

int child_func(void *args) {
    printf("child process created with pid: %d\n", getpid());
    setup_variables();
    setup_root("./root");
    mount("proc", "/proc", "proc", 0, 0);  // explicitly mounting proc filesytem at /proc, although this is usually automatically done on booting. proc is a virtual file system

    clone_process(callback_run);

    printf("procfs umounted\n");
    umount("/proc");    // good practice to release the binding but also to ensure all pending data is written
    return EXIT_SUCCESS;  
}

// Append string to end of file
void write_rule(const char* path, const char* value) {
  int fp = open(path, O_WRONLY | O_APPEND);
  write(fp, value, strlen(value));
  close(fp);
}

char* concat(const char *s1, const char *s2)
{
    char *result = malloc(strlen(s1) + strlen(s2) + 1); // +1 for the null-terminator
    // in real code you would check for errors in malloc here
    strcpy(result, s1);
    strcat(result, s2);
    return result;
}

// not working as pids not supported on WSL2 
void limitProcessCreation(int n) {
    mkdir(CGROUP_FOLDER, S_IRUSR | S_IWUSR);

    // converts parent pid to string
    char pid[16];
    sprintf(pid, "%d", getpid());
    printf("Process ID: %s\n", pid);

    char *s = concat(CGROUP_FOLDER, "cgroup.procs");
    write_rule(s, pid);
    free(s);

    s = concat(CGROUP_FOLDER, "notify_on_release");

    write_rule(s, "1");
    free(s);

    char count[5];
    sprintf(count, "%d", n);
    s = concat(CGROUP_FOLDER, "pids.max");
    write_rule(s, count);
    free(s);
}

int main(int argc, char **argv) {
    printf("Hello, World! ( parent ) \n");
    printf("parent %d\n", getpid());
    /*  1. https://www.baeldung.com/linux/fork-vfork-exec-clone - clone is upgraded fork, can use CLONE_VM flag so that child can access memory of parent. SIGCHLD flag is set so that SIGCHLD signal is sent to parent when child terminates.
        2. The "UTS namespace" specifically refers to the namespace that isolates the hostname and the domain name of the system. Each UTS namespace can have its own unique hostname and domain name. This means that processes running in different UTS namespaces can see different hostnames, even though they may be running on the same underlying system. By default, all processes on a Linux system share the same UTS namespace, and they see the same hostname. However, using the unshare system call or the unshare command-line utility, a process can create a new UTS namespace and set its own hostname independently of the system's hostname.
        3. CLONE_NEWUTS flag creates copy of the global UTS for the child process. Child has its own namespace independent of the system. Namespace holds hostname and domain name of the system.
        4. CLONE_NEWPID flag creates a separate process hierarchy (PID tree). Processes in different PID namespaces can have the same PID without conflict */
    char *stack = stack_memory(); 

    // mount pid (cgroup fs) onto cgroup/pids
    limitProcessCreation(5);
    if (clone(child_func, stack, CLONE_NEWPID | SIGCHLD | CLONE_NEWUTS, 0) == -1) {
        perror("clone");
        return EXIT_FAILURE;
    }
    
    int status; 
    if (wait(&status) == -1) {
        perror("wait");
        return EXIT_FAILURE;
    }
    free(stack-STACK_SIZE);
    return EXIT_SUCCESS;
}
