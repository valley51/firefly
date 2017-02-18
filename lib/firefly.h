//firefly.h <r68karimi[at]gmail.com>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>

class firefly {
    private:
        int _running = 1;
        int _max_events = 64000;
        int _message_size = 1024;
        struct epoll_event _event;
        struct epoll_event *_events;
        char *_port;

    public:
        firefly(char *, int);
        ~firefly();
        int on_read(char *);
        int on_connection_accept(int, char *, char *);
        int on_connection_close(int);
        int add_fd(int);
        int remove_fd(int);
        static int make_socket_non_blocking(int);
        static int create_and_bind(char *);
        int shutdown();
        int fire_event_loop();
};

//---------------- constructor, destructor --------------------
firefly::firefly(char *port, int message_size){
    _port = port;
    _message_size = message_size;
}
firefly::~firefly(){
    
}
//--------------- on read || user has to implement -------------------------------------
/*int firefly::on_read(char *buf){
    printf("%s\n", buf);
    return 1;
}*/
//--------------- on connection close -------------------------
int firefly::on_connection_accept(int fd, char* host, char* port){
    printf("Accepted connection on descriptor %d (host=%s, port=%s)\n", fd, host, port);
    return 1;
}
//--------------- on connection accept ------------------------
int firefly::on_connection_close(int fd){
    printf("Connetion on descriptor %d is closed!\n", fd);
    return 1;
}
//---------------- make socket non-blocking -------------------
int firefly::make_socket_non_blocking(int sfd) {
    int flags, s;
    flags = fcntl(sfd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl");
        return -1;
    }
    flags |= O_NONBLOCK;
    s = fcntl(sfd, F_SETFL, flags);
    if (s == -1) {
        perror("fcntl");
        return -1;
    }
    return 0;
}
//--------------- create and bind -----------------------------
int firefly::create_and_bind(char *port) {
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int s, sfd;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;     /* Return IPv4 and IPv6 choices */
    hints.ai_socktype = SOCK_STREAM; /* We want a TCP socket */
    hints.ai_flags = AI_PASSIVE;     /* All interfaces */
    s = getaddrinfo(NULL, port, &hints, &result);
    if (s != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
        return -1;
    }
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd == -1)
            continue;
        s = bind(sfd, rp->ai_addr, rp->ai_addrlen);
        if (s == 0) {
            break; /* We managed to bind successfully! */
        }
        close(sfd);
    }
    if (rp == NULL) {
        fprintf(stderr, "Could not bind\n");
        return -1;
    }
    freeaddrinfo(result);
    return sfd;
}
//---------------- shutdown -----------------------------------
int firefly::shutdown(){
    _running = 0;
    while(_running != -1) {} // just block until it's not -1
    return 0;
}
//---------------- fire event loop ----------------------------
int firefly::fire_event_loop() {
    int sfd = create_and_bind(_port);
    if (sfd == -1)
        abort();
    int s = make_socket_non_blocking(sfd);
    if (s == -1)
        abort();
    s = listen(sfd, SOMAXCONN);
    if (s == -1) {
        perror("listen");
        abort();
    }
    int efd = epoll_create1(0);
    if (efd == -1) {
        perror("epoll_create");
        abort();
    }
    _event.data.fd = sfd;
    _event.events = EPOLLIN | EPOLLET;
    s = epoll_ctl(efd, EPOLL_CTL_ADD, sfd, &_event);
    if (s == -1) {
        perror("epoll_ctl");
        abort();
    }
    /* Buffer where events are returned */
    _events = (epoll_event *) calloc(_max_events, sizeof _event);
    /* The event loop */
    while (_running) {
        int n = epoll_wait(efd, _events, _max_events, -1);
        for (int i = 0; i < n; i++) {
            if ((_events[i].events & EPOLLERR) ||
                (_events[i].events & EPOLLHUP) ||
                (!(_events[i].events & EPOLLIN))) {
                /* An error has occured on this fd, or the socket is not
                ready for reading (why were we notified then?) */
                fprintf(stderr, "epoll error\n");
                close(_events[i].data.fd);
                continue;
            } else if (sfd == _events[i].data.fd) {
                /* We have a notification on the listening socket, which
                means one or more incoming connections. */
                while (1) {
                    struct sockaddr in_addr;
                    socklen_t in_len;
                    int infd;
                    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

                    in_len = sizeof in_addr;
                    infd = accept(sfd, &in_addr, &in_len);
                    if (infd == -1) {
                        if ((errno == EAGAIN) ||
                            (errno == EWOULDBLOCK)) {
                            /* We have processed all incoming
                            connections. */
                            break;
                        } else {
                            perror("accept");
                            break;
                        }
                    }
                    s = getnameinfo(&in_addr, in_len,
                                    hbuf, sizeof hbuf,
                                    sbuf, sizeof sbuf,
                                    NI_NUMERICHOST | NI_NUMERICSERV);
                    if (s == 0) {
                        
                        /*----------------------*/
                        on_connection_accept(infd, hbuf, sbuf);
                        /*----------------------*/
                    }
                    /* Make the incoming socket non-blocking and add it to the
                    list of fds to monitor. */
                    s = make_socket_non_blocking(infd);
                    if (s == -1)
                        abort();

                    _event.data.fd = infd;
                    _event.events = EPOLLIN | EPOLLET;
                    s = epoll_ctl(efd, EPOLL_CTL_ADD, infd, &_event);
                    if (s == -1) {
                        perror("epoll_ctl");
                        abort();
                    }
                }
                continue;
            } else {
                /* We have data on the fd waiting to be read. Read and
                display it. We must read whatever data is available
                completely, as we are running in edge-triggered mode
                and won't get a notification again for the same
                data. */
                int done = 0;

                while (1) {
                    ssize_t count;
                    char buf[_message_size] = {'\0'};

                    count = read(_events[i].data.fd, buf, _message_size);
                    if (count == -1) {
                        /* If errno == EAGAIN, that means we have read all
                        data. So go back to the main loop. */
                        if (errno != EAGAIN) {
                            perror("read");
                            done = 1;
                        }
                        break;
                    } else if (count == 0) {
                        /* End of file. The remote has closed the
                        connection. */
                        done = 1;
                        break;
                    }

                    int tmp = count;

                    if (count == _message_size)
                        tmp = -1;

                    while (tmp != -1 && tmp < _message_size) {
                        count = read(_events[i].data.fd, buf, _message_size - tmp);
                        if (count <= 0) {
                            continue;
                        }
                        tmp += count;
                    }

                    /* ------------ */
                    on_read(buf);
                    /* ------------ */

                }

                if (done) {
                    /* Closing the descriptor will make epoll remove it from the set of descriptors which are monitored. */
                    close(_events[i].data.fd);
                    /* ------------ */
                    on_connection_close(_events[i].data.fd);
                    /* ------------ */
                }
            }
        }
    }

    free(_events);
    close(sfd);
    _running = -1; // it means that shutdown was successfull
    return 1;
}


