#include <iostream>
#include <netdb.h>
#include <fcntl.h>
#include <unordered_map>

#include <sys/epoll.h>
#include <sys/stat.h>
#include <zconf.h>
#include <vector>
#include "parsers.h"
#include "run_piped.h"

using namespace std;

int make_socket_non_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
        return -1;
    flags |= O_NONBLOCK;
    return fcntl(fd, F_SETFL, flags);
}

int make_socket_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
        return -1;
    flags &= ~O_NONBLOCK;
    return fcntl(fd, F_SETFL, flags);
}

int make_server(char *port) {
    struct addrinfo *localhost;
    if (getaddrinfo("0.0.0.0", port, 0, &localhost)) {
        return -1;
    }
    int sock = socket(localhost->ai_family, SOCK_STREAM, 0);
    if (sock < 0) {
        return -2;
    }
    if (bind(sock, localhost->ai_addr, localhost->ai_addrlen)) {
        return -3;
    }
    if (make_socket_non_blocking(sock) < 0) {
        return -4;
    }
    if (listen(sock, SOMAXCONN)) {
        return -5;
    }
    freeaddrinfo(localhost);
    return sock;
}

unordered_map<int, string> buffers;

int add_client(int efd, int client) {
    //cerr << "new client: " << client << "\n";
    if (client < 0)
        return -11;
    if (make_socket_non_blocking(client) < 0)
        return -12;
    struct epoll_event event;
    event.data.fd = client;
    event.events = EPOLLIN | EPOLLET;
    if (epoll_ctl(efd, EPOLL_CTL_ADD, client, &event) < 0)
        return -13;
    buffers.insert(make_pair(client, ""));
    return client;
}

int start_prog(vector<execargs_t>& prog, int fd) {
    if (make_socket_blocking(fd) < 0)
        return -1;
    int pid = fork();
    if (pid < 0) {
        return -1;
    } else if (pid) {
        //cerr<<"chld: "<<pid<<"\n";
        for (int i = 0; i < prog.size(); i++)
            execargs_free(prog[i]);
        prog.clear();
        close(fd);//only child handle descriptor now
        //ignore children, they will finish somehow
        return 0;
    } else {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        int res = run_piped(prog);
        close(fd);
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        exit(res);
    }
}

void remove_socket(int efd, struct epoll_event* event, int fd) {
    epoll_ctl(efd, EPOLL_CTL_DEL, fd, event);
    buffers.erase(buffers.find(fd));
}

void make_deamon() {
    pid_t pid = fork();
    if (pid < 0) {
        exit(1);
    }
    if (pid > 0) {
        FILE* file = fopen("/tmp/netsh.pid","w");
        if (file != NULL) {
            char str[15];
            sprintf(str, "%d", pid);
            fputs(str, file);
            fclose(file);
            exit(0);
        }
        else {//can't create file - kill child
            kill(pid, SIGKILL);
            exit(1);
        }
    }

    umask(0);

    pid_t sid = setsid();
    if (sid < 0) {
        exit(1);
    }

    if ((chdir("/")) < 0) {
        exit(1);
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s [port]\n", argv[0]);
        return 1;
    }

    int server = make_server(argv[1]);
    if (server < 0)
        return 2;

    int efd = epoll_create1(0);
    if (efd == -1) {
        return 3;
    }

    make_deamon();

    struct epoll_event event;
    struct epoll_event *events;
    size_t events_size = 1;

    event.data.fd = server;
    event.events = EPOLLIN | EPOLLET;
    if (epoll_ctl(efd, EPOLL_CTL_ADD, server, &event) < 0)
        return 4;

    events = (epoll_event *) calloc(events_size, sizeof(event));

    while (1) {
        if (events_size != buffers.size()+1) {
            events_size = buffers.size()+1;
            free(events);
            events = (epoll_event *) calloc(events_size, sizeof(event));
        }
        int n = epoll_wait(efd, events, (int) events_size, -1);
        for (int i = 0; i < n; i++) {
            int cur_fd = events[i].data.fd;
            if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP)
                || (events[i].events & EPOLLRDHUP)) {
                //cerr << "error on socket: " << cur_fd << "\n";
                if (server != cur_fd) {//server should be alive as long as possible
                    remove_socket(efd, &events[i], cur_fd);
                    close(cur_fd);
                }
                continue;
            }
            else if (server == cur_fd) {
                int res = accept(server, 0, 0);
                res = add_client(efd, res);
                if (res < 0)
                    return -res;
                continue;
            }
            else {
                char buf[1];
                bool read_finished = false;
                //read command on byte per read(), while '\n' not found
                //command length in general is much smaller than command input,
                //so we won't have perfomance issues, but will simplify code
                while (1) {
                    ssize_t count = read(cur_fd, buf, 1);
                    if (count == 0) {
                        //cerr << "on socket " << cur_fd << " close\n";
                        remove_socket(efd, &events[i], cur_fd);
                        close(cur_fd);
                        break;
                    }
                    else if (count == -1) {
                        if (errno != EAGAIN) {//real error happened, close socket
                            remove_socket(efd, &events[i], cur_fd);
                            close(cur_fd);
                        }
                        break;
                    }
                    else {
                        buffers.find(cur_fd)->second += buf[0];
                        if (buf[0] == '\n') {
                            read_finished = true;
                            break;
                        }
                    }
                }
                if (read_finished) {
                    //cerr<<"command: "<<buffers.find(cur_fd)->second<<"\n";
                    vector<execargs_t> prog;
                    if (parse_command(buffers.find(cur_fd)->second, prog) >= 0) {
                        remove_socket(efd, &events[i], cur_fd);
                        start_prog(prog, cur_fd);
                    } else {
                        //cerr << "can't parse program on sock " << cur_fd << "\n";
                        remove_socket(efd, &events[i], cur_fd);
                        close(cur_fd);
                    }
                }
            }
        }
    }
}
