#ifndef OS_EXTRA_HELPERS_H
#define OS_EXTRA_HELPERS_H

#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <vector>

#include "helper.h"

using namespace std;

void cleanup(vector<pid_t>& pids, sigset_t& mask) {
    for(size_t i = 0; i < pids.size(); i++) {
        if(pids[i]) {
            kill(pids[i], SIGKILL);
            waitpid(pids[i], 0, 0);
        }
    }
    sigprocmask(SIG_SETMASK, &mask, 0);
}

int run_piped(vector<execargs_t> args) {
    size_t n = args.size();
    int pipes[2*n - 2];
    for(size_t i = 1; i < n; i++) {
        int res = pipe2(pipes + 2 * (i - 1), O_CLOEXEC);
        if(res) {
            for(int j = 1; j < i; j++) {
                close(pipes[j * 2 - 2]);
                close(pipes[j * 2 - 1]);
            }
            return -1;
        }
    }

    vector<pid_t> pids(n);

    sigset_t mask;
    sigset_t orig_mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, &orig_mask);

    int fork_failed = 0;
    for(size_t i = 0; i < n; i++) {
        int pid = fork();
        if(pid < 0) {
            fork_failed = 1;
            goto no_forks;
        } else if(pid) {
            pids[i] = pid;
        } else {
            if(i > 0)
                dup2(pipes[i * 2 - 2], STDIN_FILENO);
            if(i < n - 1)
                dup2(pipes[i * 2 + 1], STDOUT_FILENO);
            sigprocmask(SIG_SETMASK, &orig_mask, 0);
            exit(exec(args[i]));
        }
    }
    no_forks:

    for(size_t i = 1; i < n; i++) {
        close(pipes[i * 2 - 2]);
        close(pipes[i * 2 - 1]);
    }

    if(fork_failed) {
        cleanup(pids, orig_mask);
        return -1;
    }

    siginfo_t info;
    int killed_procs = 0;
    while(1) {
        sigwaitinfo(&mask, &info);
        if(info.si_signo == SIGINT)
            break;
        if(info.si_signo == SIGCHLD) {
            int chld;
            while((chld = waitpid(-1, 0, WNOHANG)) > 0) {
                for(int i = 0; i < n; i++) {
                    if(pids[i] == chld) {
                        pids[i] = 0;
                        break;
                    }
                }
                killed_procs++;
                if(killed_procs == n) {
                    cleanup(pids, orig_mask);
                    return 0;
                }
            }
        }
    }
    cleanup(pids, orig_mask);
    return 0;
}
#endif //OS_EXTRA_HELPERS_H
