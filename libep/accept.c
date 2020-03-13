#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/epoll.h>
#include "ae_epoll.h"

char *tmpbuf = NULL;

void freeClient(aeloop_t* mainloop, int fd){
    int ret=-1;
    (void)del_socket_fd_event(mainloop,fd,AE_READABLE,AE_USE_RDHUP);
    (void)del_socket_fd_event(mainloop,fd,AE_WRITABLE,AE_USE_RDHUP);
    ret = close(fd);
    if (ret <0 ){
        printf("close fd error %d %s",errno,strerror(errno));
    }
    return;
}

void handleUserData(aeloop_t* mainloop,int client_fd,int mask,void *data){
    int nread = 0;
    char buf[512]= {0};
    int readlen = 10;

    printf("handleUserData read\n");
    
    nread = read(client_fd,buf,readlen);
    if (nread==-1){
        if (errno == EAGAIN){
            printf("xxx %d %s\n",errno,strerror(errno));
            return;
        }else{
            //1:if client creash ,tcp keepalive will timeout,
            //  server send rst to client, and here may  get errno 110,connection timeout
            //2: if client send rst to server, 
            //   here may get errno 104,Connection reset by peer
            printf("read from client error %d %s\n",errno,strerror(errno));
            freeClient(mainloop,client_fd);
            return;
        }
    }else if (nread==0){
        printf("client closed \n");
        freeClient(mainloop,client_fd);
        return;
    }

    printf("read from client %s(len:%d)\n",buf,nread);

    return;

}

void writeToUserData(aeloop_t *mainloop, int client_fd,int mask, void*data){
    int writen = -1;
    printf("handle write is %s \n",(data == NULL)? "NULLL":"NOT NULLL" );
    writen = write(client_fd,tmpbuf,400);
    printf("write no = %d\n",writen);

}

void handleAccept(aeloop_t* mainloop,int lsfd,int mask,void *data){
    struct  sockaddr_in client_addr;
    socklen_t len = sizeof(struct  sockaddr_in);
    memset(&client_addr,0,sizeof(struct sockaddr_in ));
    int client_fd = 0;
    int ret = -1;
    while (1)
    {
        //惊群在理论上是一定存在的，但是在实际中不一定是必现的，尤其是单核机上，如果要使其必现，
        //accept之前可以短暂sleep一段时间，让这个可读事件一直存在一段时间，这样就能唤醒所有进程，
        //当然只有一个进程能accept成功，其他进程都返回 EAGIN
        //usleep(10000); 
        client_fd = accept(lsfd,(struct sockaddr*)&client_addr,&len);
        //printf("process %d get client %d errno:%d errstring=%s\n", getpid(),client_fd,errno,strerror(errno));
        if (client_fd == -1){
            if (errno == EINTR){
                continue;
            }else{
                /*may return Resource temporarily unavailable errno 11*/
                /*产生原因：epoll 惊群: 多个进程 epoll_wait listen_fd时，
                 有新的tcp链接过来时，可能会唤醒多个进程，但是只有一个进程会正确的accept新的链接，
                 其余的进程会产生错误.
                 EPOLLEXCLUSIVE 会解决该问题? */
                printf("pid %d: accept errno = %d:%s\n",getpid(),errno,strerror(errno));
                return;
            }
        }
        break;
    }
    
    /*just print */
    printf("process %d accept client %s:%d\n",getpid(),inet_ntoa(client_addr.sin_addr),ntohs(client_addr.sin_port));

    //don't need set client fd keepalive, inherited from listen fd
    //set_socket_fd_keepalive(client_fd,10);
    setSocketBlock(client_fd,1);

    //sndbuf: inherited from listen fd
    //get_socket_fd_sndbuf(client_fd);

    add_socket_fd_event(mainloop,client_fd,AE_READABLE,AE_USE_RDHUP,handleUserData,NULL);
    
    //add_socket_fd_event(mainloop,client_fd,AE_WRITABLE,1,writeToUserData,NULL);
    
}

int add_accept_handle(aeloop_t* mainloop, int lsfd, 
                        aeEventProc *handleAccept,
                        void *data){
    return add_socket_fd_event(mainloop,lsfd,AE_READABLE,AE_USE_EXCLUSIVE ,handleAccept,data);
}


aeloop_t *mainloop = NULL;

void main(){
    struct sockaddr addr_in;
    
    struct sockaddr_in server;
    struct sockaddr_in client;
    socklen_t len = 0;
    int client_fd = 0;
    int reuse_addr = 0;
    int process_loop = 0;
    
    pid_t pid;
    int ret = -1;
    
    tmpbuf =  (char*)malloc(sizeof(char) * 400000);
    memset(tmpbuf,'a',sizeof(char)*400000);

    
    /*create listening fd*/
    int ls = -1;
    ls = socket(AF_INET,SOCK_STREAM,IPPROTO_IP);
    if (ls == -1){
        printf("create socket error\n");
        return;
    }else{
        printf("ls=%d\n",ls);
    }


    /*set listening fd options*/
    ret = setSocketBlock(ls,1);
    if (ret<0){
        printf("setSocketBlock error\n");
    }
    int yes = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    set_socket_fd_keepalive(ls,10);    
    set_socket_fd_sndbuf(ls,4*1024);
    

    server.sin_family = AF_INET;
    server.sin_port = htons((unsigned short)8999);
    server.sin_addr.s_addr = INADDR_ANY;
    if(bind(ls,(struct sockaddr *)&server, sizeof(server)) < 0 ){
        printf("bind error %s %d\n",strerror(errno),errno);
        return;
    }

    listen(ls,5);

    for (process_loop=0;process_loop<4;process_loop++){
        pid = fork();
        if (pid<0){

        }else{
            if (pid == 0){
                
                ret = create_epoll_loop(&mainloop,1024);
                if (ret<0 || mainloop == NULL){
                    printf("init err\n");
                    return ;
                }

                add_accept_handle(mainloop,ls,handleAccept,NULL);
                while (1){
                        //sort the miniest time
                        struct timeval tvp = {0};
                        int j = 0;
                        tvp.tv_sec = 100;
                        tvp.tv_usec = 0;
                        
                        (void)aeProcessEvents(mainloop,&tvp);
                    }
            }else{
                printf("create pid %d\n",pid);
            }
        }
    }

    sleep(-1);
    
}
