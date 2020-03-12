CC=gcc
STLIB_MAKE_CMD=ar rcs 
server_name=accept
lib_name=libep.a
lib_obj=ae_epoll.o

all:$(server_name)
clean:
	rm -rf $(server_name) *.o *.a

%.o: %.c
	$(CC) -c $<	

$(lib_name): $(lib_obj)
	$(STLIB_MAKE_CMD) $(lib_name) $(lib_obj)
	


$(server_name): accept.c $(lib_name)
	$(CC) -o $@ -I. $< $(lib_name)
