include ../Make.defines

PROGS =	tcpcli01 tcpcli04 tcpcli05 tcpcli06 ass1cli ass1cli2 ass1serv ass3 ass4 ass4serv ass5serv ass5cli\
		tcpcli07 tcpcli08 tcpcli09 tcpcli10 \
		tcpserv01 tcpserv02 tcpserv03 tcpserv04 server client\
		tcpserv08 tcpserv09 tcpservselect01 tcpservpoll01 tsigpipe

all:	${PROGS}

server:	server.o
		${CC} ${CFLAGS} -o $@ server.o ${LIBS}

client:	client.o
		${CC} ${CFLAGS} -o $@ client.o ${LIBS}

tcpcli01:	tcpcli01.o
		${CC} ${CFLAGS} -o $@ tcpcli01.o ${LIBS}

ass6serv:	ass6serv.o
		${CC} ${CFLAGS} -o $@ ass6serv.o ${LIBS}

ass6cli:	ass6cli.o
		${CC} ${CFLAGS} -o $@ ass6cli.o ${LIBS}

ass5serv:	ass5serv.o
		${CC} ${CFLAGS} -o $@ ass5serv.o ${LIBS}

ass5cli:	ass5cli.o
		${CC} ${CFLAGS} -o $@ ass5cli.o ${LIBS}

ass4:	ass4.o
		${CC} ${CFLAGS} -o $@ ass4.o ${LIBS}


ass4serv:	ass4serv.o
		${CC} ${CFLAGS} -o $@ ass4serv.o ${LIBS}
		
ass3:	ass3.o
		${CC} ${CFLAGS} -o $@ ass3.o ${LIBS}
		
tcpcli04:	tcpcli04.o
		${CC} ${CFLAGS} -o $@ tcpcli04.o ${LIBS}

tcpcli05:	tcpcli05.o
		${CC} ${CFLAGS} -o $@ tcpcli05.o ${LIBS}

tcpcli06:	tcpcli06.o
		${CC} ${CFLAGS} -o $@ tcpcli06.o ${LIBS}

tcpcli07:	tcpcli07.o
		${CC} ${CFLAGS} -o $@ tcpcli07.o ${LIBS}

tcpcli08:	tcpcli08.o str_cli08.o
		${CC} ${CFLAGS} -o $@ tcpcli08.o str_cli08.o ${LIBS}

tcpcli09:	tcpcli09.o str_cli09.o
		${CC} ${CFLAGS} -o $@ tcpcli09.o str_cli09.o ${LIBS}

tcpcli10:	tcpcli10.o
		${CC} ${CFLAGS} -o $@ tcpcli10.o ${LIBS}

tcpcli11:	tcpcli11.o str_cli11.o
		${CC} ${CFLAGS} -o $@ tcpcli11.o str_cli11.o ${LIBS}

tcpserv01:	tcpserv01.o
		${CC} ${CFLAGS} -o $@ tcpserv01.o ${LIBS}

tcpserv02:	tcpserv02.o sigchldwait.o
		${CC} ${CFLAGS} -o $@ tcpserv02.o sigchldwait.o ${LIBS}

tcpserv03:	tcpserv03.o sigchldwait.o
		${CC} ${CFLAGS} -o $@ tcpserv03.o sigchldwait.o ${LIBS}

tcpserv04:	tcpserv04.o sigchldwaitpid.o
		${CC} ${CFLAGS} -o $@ tcpserv04.o sigchldwaitpid.o ${LIBS}

tcpserv08:	tcpserv08.o str_echo08.o sigchldwaitpid.o
		${CC} ${CFLAGS} -o $@ tcpserv08.o str_echo08.o sigchldwaitpid.o \
			${LIBS}

tcpserv09:	tcpserv09.o str_echo09.o sigchldwaitpid.o
		${CC} ${CFLAGS} -o $@ tcpserv09.o str_echo09.o sigchldwaitpid.o \
			${LIBS}

tcpservselect01:	tcpservselect01.o
		${CC} ${CFLAGS} -o $@ tcpservselect01.o ${LIBS}

tcpservpoll01:	tcpservpoll01.o
		${CC} ${CFLAGS} -o $@ tcpservpoll01.o ${LIBS}

tsigpipe:	tsigpipe.o
		${CC} ${CFLAGS} -o $@ tsigpipe.o ${LIBS}
		
ass1cli:	ass1cli.o
		${CC} ${CFLAGS} -o $@ ass1cli.o ${LIBS}

ass1cli2:	ass1cli2.o
		${CC} ${CFLAGS} -o $@ ass1cli2.o ${LIBS}
		
ass1serv:	ass1serv.o
		${CC} ${CFLAGS} -o $@ ass1serv.o ${LIBS}

clean:
		rm -f ${PROGS} ${CLEANFILES}
