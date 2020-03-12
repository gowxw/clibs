
#define AE_NONE 0
#define AE_READABLE 1
#define AE_WRITABLE 2

typedef void aeEventProc(struct aeloop_t* mainloop, int fd, int mask, void*data);

typedef struct aeEvents_s {
    int mask;
    aeEventProc *readProc;
    aeEventProc *writeProc;
    void *clientData;
}aeEvent;

typedef struct aeFiredEvent_s
{
    int fd;
    int mask;
}aeFiredEvent;

typedef struct aeloop_t {
    int ep;
    int setsize;
    aeEvent *events;
    aeFiredEvent * fired;
    void * apiEvents;  // returned Events from select/poll/epoll_wait/
    
}aeloop_t;




int setSocketBlock(int fd, int non_block);
int getSocketBlock(int fd);
int set_socket_fd_keepalive(int fd,int interval);

int create_epoll_loop(aeloop_t** loop, int configSize);

int addEvent(aeloop_t* mainloop, int fd, int mask, int flag);
int delEvent(aeloop_t* mainloop, int fd, int mask, int flag);

int add_socket_fd_event(aeloop_t* mainloop, int fd,int mask,int flag,
                           aeEventProc *proc,void* userData);

int del_socket_fd_event(aeloop_t* mainloop, int fd,int mask,int flag);

int aePoll(aeloop_t* mainloop, struct timeval *tvp);
int aeProcessEvents(aeloop_t* mainloop, struct timeval *tvp);

int get_socket_fd_sndbuf(int fd);
int set_socket_fd_sndbuf(int fd, int bufsize);
