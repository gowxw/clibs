#include <stdio.h>
#include <sys/epoll.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <linux/in.h>
#include <linux/tcp.h>

#include "ae_epoll.h"

int aeCreate(aeloop_t *m){
    if (m==NULL){
        return -1;
    }

    m->ep = epoll_create(1024);
    if (m->ep < 0){
        printf("%s\n","create ep fd error");
        return -1;
    }

    return 0;
}

int create_epoll_loop(aeloop_t** loop, int configSize){
    aeloop_t *mainloop = NULL;

    mainloop = (aeloop_t*)malloc(sizeof(aeloop_t));
    if (mainloop == NULL){
        printf("%s\n","malloc mainloop error");
        return -1;
    }
    mainloop->ep = -1;
    mainloop->events = NULL;
    mainloop->fired = NULL;
    mainloop->apiEvents = NULL;
    mainloop->setsize = configSize;

    if (aeCreate(mainloop) < 0 ){
        printf("%s\n","create ep");
    }

    mainloop->events = malloc(sizeof(aeEvent)*configSize);
    if (mainloop->events == NULL){
        printf("%s\n","malloc mainloop  events error");
        return -1;
    }
    memset(mainloop->events,0,sizeof(aeEvent)*configSize);

    mainloop->fired = malloc(sizeof(aeFiredEvent)*configSize);
    if (mainloop->fired == NULL){
        printf("%s\n","malloc mainloop  fired error");
        return -1;
    }
    memset(mainloop->fired,0,sizeof(aeFiredEvent)*configSize);

    mainloop->apiEvents = malloc( sizeof(struct epoll_event) * configSize);
    if (mainloop->apiEvents == NULL){
        printf("%s\n","malloc mainloop  fired error");
        return -1;
    }
    memset(mainloop->apiEvents,0,sizeof(struct epoll_event) * configSize);

    *loop = mainloop;
    return 0;

}



int addEvent(aeloop_t* mainloop, int fd, int mask, int flag){
    int newmask = 0;
    int op = 0;
    struct epoll_event ee={0};

    op = (mainloop->events[fd].mask == AE_NONE) ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;
    newmask = mainloop->events[fd].mask | mask;

    ee.events = 0;
    if (newmask&AE_READABLE){
        ee.events |= EPOLLIN;
    }

    if (newmask&AE_WRITABLE){
        ee.events |= EPOLLOUT;
    }

    if (flag & AE_USE_EXCLUSIVE){
        ee.events = ee.events | EPOLLEXCLUSIVE;
    }

    if (flag & AE_USE_RDHUP){
        ee.events = ee.events | EPOLLRDHUP | EPOLLET;
    }



    ee.data.fd = fd;
    printf("add fd:%d envents:0x%x op=%s\n",fd,ee.events, (op==EPOLL_CTL_ADD)?"EPOLL_CTL_ADD":"EPOLL_CTL_MOD");
    if (epoll_ctl(mainloop->ep,op,fd,&ee) <0){
        printf("add %d to %d,%s %d:%s\n",fd,mainloop->ep,"epoll_ctl err",errno,strerror(errno));
        return -1;
    }
    return 0; 
}

int delEvent(aeloop_t* mainloop, int fd, int mask, int flag){
    int newmask = 0;
    struct epoll_event ee={0};
    int ret = -1;
    
    
    newmask = mainloop->events[fd].mask & (~mask);

    ee.events = 0;
    if (newmask&AE_READABLE){
        ee.events |= EPOLLIN;
    }
    if (newmask&AE_WRITABLE){
        ee.events |= EPOLLOUT;
    }

    if (flag & AE_USE_EXCLUSIVE){
        ee.events = ee.events | EPOLLEXCLUSIVE;
    }

    if (flag & AE_USE_RDHUP){
        ee.events = ee.events | EPOLLRDHUP | EPOLLET;
    }


    ee.data.fd = fd;

    if (newmask != AE_NONE){      
        printf("del mod fd:%d envents:0x%x\n",fd,ee.events);
        ret = epoll_ctl(mainloop->ep, EPOLL_CTL_MOD, fd, &ee);
        if (ret<0){
            printf("delEvent epoll_ctl error\n");
        }
    }else{
        printf("del del fd:%d envents:0x%x\n",fd,ee.events);
        ret = epoll_ctl(mainloop->ep, EPOLL_CTL_DEL, fd, &ee);
        if (ret<0){
            printf("delEvent epoll_ctl error\n");
        }
    }
    return ret;
    
}

int aePoll(aeloop_t* mainloop, struct timeval *tvp){
    int ret = 0;
    int retevents = 0;
    int j = 0;
    int mask = 0;
    retevents = epoll_wait(mainloop->ep, (struct epoll_event*)mainloop->apiEvents, 
                            mainloop->setsize, 
                            tvp? (tvp->tv_sec*1000 + tvp->tv_usec/1000):-1);


    if (retevents>0){
        for (j=0;j<retevents;j++){
            struct epoll_event *ev = (struct epoll_event*)mainloop->apiEvents + j;
            printf("epoll_wait ret 0x%x\n",ev->events);
            //printf("event coming: events:%d fd:%d\n",ev->events,ev->data.fd);
            if (ev->events & EPOLLIN){
                mask |= AE_READABLE;
            }
            if (ev->events & EPOLLOUT){
                mask |= AE_WRITABLE;
            }
            if (ev->events & EPOLLERR){
                mask |= AE_WRITABLE;
            }
            if (ev->events & EPOLLHUP){
                mask |= AE_WRITABLE;
            }

            mainloop->fired[j].fd = ev->data.fd;
            mainloop->fired[j].mask = mask;
        }
    }

    return retevents;

}


int aeProcessEvents(aeloop_t* mainloop, struct timeval *tvp){
    int eventsnum = 0;
    int j = 0;
    eventsnum = aePoll(mainloop, tvp);
    printf("eventsnum = %d pid=%d\n",eventsnum,getpid());
    if (eventsnum>0){
        for (j=0; j < eventsnum; j++){
            int mask = mainloop->fired[j].mask;
            int fd   = mainloop->fired[j].fd;

            aeEvent * ev = &mainloop->events[fd];
            if (ev->mask & mask & AE_READABLE){
                printf("pid:%d %d %d %s 0x%x\n",getpid(),fd,mask,(char*)(ev->clientData),ev->readProc);
                ev->readProc(mainloop,fd,mask,ev->clientData);
            }

            if (ev->mask & mask & AE_WRITABLE){
                printf("pid:%d %d %d %s 0x%x\n",getpid(),fd,mask,(char*)(ev->clientData),ev->writeProc);
                ev->writeProc(mainloop,fd,mask,ev->clientData);
            }
        }
    }else if (eventsnum==0){
        int writen = 0;
        int writelen = 1;

    }
    return eventsnum;

}


int add_socket_fd_event(aeloop_t* mainloop, int fd,int mask,int flag,
                           aeEventProc *proc,void* userData){
    int ret = -1;
    aeEvent *  ev = &mainloop->events[fd];
    
    ret = addEvent(mainloop,fd,mask,flag);
    if (ret == -1){
        return ret;
    }

    //printf("mask=%d,rd:0x%x wr:0x%x\n",mask,ev->readProc,ev->writeProc);
    if (mask & AE_READABLE){
        ev->readProc = proc;
    }

    if (mask & AE_WRITABLE){
        ev->writeProc = proc;
    }
    //printf("mask=%d,rd:0x%x wr:0x%x\n",mask,ev->readProc,ev->writeProc);

    ev->mask |= mask;
    ev->clientData = userData;
    return 0;

}



int del_socket_fd_event(aeloop_t* mainloop, int fd,int mask, int flag){

    aeEvent * ev = &mainloop->events[fd];
    if (ev->mask == AE_NONE){
        return;
    }

    delEvent(mainloop,fd,mask,flag);
    ev->mask = ev->mask & (~mask);

}

int getSocketBlock(int fd){
    int flags = 0;
    if ((flags = fcntl(fd, F_GETFL)) == -1) {
        return -1;
    }

    return (flags & O_NONBLOCK);
    
}

int setSocketBlock(int fd, int non_block) {
    int flags = 0;

    if ((flags = fcntl(fd, F_GETFL)) == -1) {
        return -1;
    }

    if (non_block)
        flags |= O_NONBLOCK;
    else
        flags &= ~O_NONBLOCK;

    if (fcntl(fd, F_SETFL, flags) == -1) {
        
        return -1;
    }
    return 0;
}

int set_socket_fd_keepalive(int fd,int interval){
    int var = 1;
    int ret = -1;
    ret = setsockopt(fd,SOL_SOCKET,SO_KEEPALIVE,&var,sizeof(var));
    if (ret<0){
        printf("%s %d:%s\n","setsockopt err",errno,strerror(errno));
        return -1;
    }

    var = interval;

    ret = setsockopt(fd,IPPROTO_TCP,TCP_KEEPIDLE,&var,sizeof(var));
    if (ret<0){
        printf("%s %d:%s\n","setsockopt err",errno,strerror(errno));
        return -1;
    }

    var = interval/3;
    ret = setsockopt(fd,IPPROTO_TCP,TCP_KEEPINTVL,&var,sizeof(var));
    if (ret<0){
        printf("%s %d:%s\n","setsockopt err",errno,strerror(errno));
        return -1;
    }

    var = 3;
    ret = setsockopt(fd,IPPROTO_TCP,TCP_KEEPCNT,&var,sizeof(var));
    if (ret<0){
        printf("%s %d:%s\n","setsockopt err",errno,strerror(errno));
        return -1;
    }

    return 0;

}

int get_socket_fd_sndbuf(int fd){
    int buflen = 0;
    int len = sizeof(int);
    int ret = -1;

    ret = getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buflen, &len);
    if (ret < 0){
        printf("get SO_SNDBUF error\n");
    }else{
        printf("get SO_SNDBUF : fd:%d sendbuf_len:%d\n",fd,buflen);
    }
    return 0;

}

int set_socket_fd_sndbuf(int fd, int bufsize){
    int ret = -1;

    ret = setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(int));
    if (ret < 0){
        printf("set SO_SNDBUF error\n");
    }else{
        printf("set SO_SNDBUF : %d\n",bufsize);
    }
    return 0;

}