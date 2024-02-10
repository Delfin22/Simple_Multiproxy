 #include <arpa/inet.h>
       #include <errno.h>
       #include <netinet/in.h>
       #include <signal.h>
       #include <stdio.h>
       #include <stdlib.h>
       #include <string.h>
       #include <sys/select.h>
       #include <sys/socket.h>
       #include <unistd.h>
struct Proxy{
	int local_port;
	int host_port;
	char *adress;
};
       static int forward_port;
       #undef max
       #define max(x, y) ((x) > (y) ? (x) : (y))

       static int
       listen_socket(int listen_port)
       {
           int                 lfd;
           int                 yes;
           struct sockaddr_in  addr;

           lfd = socket(AF_INET, SOCK_STREAM, 0);
           if (lfd == -1) {
               perror("socket");
               return -1;
           }

           yes = 1;
           if (setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR,
                          &yes, sizeof(yes)) == -1)
           {
               perror("setsockopt");
               close(lfd);
               return -1;
           }

           memset(&addr, 0, sizeof(addr));
           addr.sin_port = htons(listen_port);
           addr.sin_family = AF_INET;
           if (bind(lfd, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
               perror("bind");
               close(lfd);
               return -1;
           }

           printf("accepting connections on port %d\n", listen_port);
           listen(lfd, 10);
           return lfd;
       }

       static int
       connect_socket(int connect_port, char *address)
       {
           int                 cfd;
           struct sockaddr_in  addr;

           cfd = socket(AF_INET, SOCK_STREAM, 0);
           if (cfd == -1) {
               perror("socket");
               return -1;
           }

           memset(&addr, 0, sizeof(addr));
           addr.sin_port = htons(connect_port);
           addr.sin_family = AF_INET;

           if (!inet_aton(address, (struct in_addr *) &addr.sin_addr.s_addr)) {
               fprintf(stderr, "inet_aton(): bad IP address format\n");
               close(cfd);
               return -1;
           }

           if (connect(cfd, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
               perror("connect()");
               shutdown(cfd, SHUT_RDWR);
               close(cfd);
               return -1;
           }
           return cfd;
       }

       #define SHUT_FD1 do {                                \
                            if (fd1 >= 0) {                 \
                                shutdown(fd1, SHUT_RDWR);   \
                                close(fd1);                 \
                                fd1 = -1;                   \
                            }                               \
                        } while (0)

       #define SHUT_FD2 do {                                \
                            if (fd2 >= 0) {                 \
                                shutdown(fd2, SHUT_RDWR);   \
                                close(fd2);                 \
                                fd2 = -1;                   \
                            }                               \
                        } while (0)

       #define BUF_SIZE 1024

       int
       main(int argc, char *argv[])
       {
           int      h;
           int      ready, nfds;
           int      fd1 = -1, fd2 = -1;
           int      buf1_avail = 0, buf1_written = 0;
           int      buf2_avail = 0, buf2_written = 0;
           char     buf1[BUF_SIZE], buf2[BUF_SIZE];
           fd_set   readfds, writefds, exceptfds;
           ssize_t  nbytes;

	   struct Proxy *proxies = (struct Proxy *)calloc(argc-1,sizeof(struct Proxy));
	   char *temp;
	   char *token;
	   int pid;
	   int *pids = (int*)calloc(argc-1,sizeof(int));

           if (argc < 2) {
               fprintf(stderr, "Usage\n\t <local-port> "
                       "<forward-to-ip-address> <forward-port>\n");
               exit(EXIT_FAILURE);
           }

           signal(SIGPIPE, SIG_IGN);



	  for(int i = 0; i < argc - 1; i++){
	  	temp = argv[i + 1];
		token = strtok(temp,":");
		if(token != NULL){
			proxies[i].local_port = atoi(token);
			token = strtok(NULL,":");
			if(token != NULL){
				proxies[i].adress = token;
				token = strtok(NULL,":");
				if(token != NULL){
					proxies[i].host_port = atoi(token);
				}
			}
		}
	}


	



for(int i = 0; i < argc-1; i++){
	   if((pid = fork()) < 0){
		perror("Fork");
		exit(EXIT_FAILURE);
	   }
	   else if (pid == 0){

           forward_port = proxies[i].host_port;

           h = listen_socket(proxies[i].local_port);
           if (h == -1)
               exit(EXIT_FAILURE);

           for (;;) {
               nfds = 0;

               FD_ZERO(&readfds);
               FD_ZERO(&writefds);
               FD_ZERO(&exceptfds);
               FD_SET(h, &readfds);
               nfds = max(nfds, h);

               if (fd1 > 0 && buf1_avail < BUF_SIZE)
                   FD_SET(fd1, &readfds);
                   // Note: nfds is updated below, when fd1 is added to
                     // exceptfds. 
               if (fd2 > 0 && buf2_avail < BUF_SIZE)
                   FD_SET(fd2, &readfds);

               if (fd1 > 0 && buf2_avail - buf2_written > 0)
                   FD_SET(fd1, &writefds);
               if (fd2 > 0 && buf1_avail - buf1_written > 0)
                   FD_SET(fd2, &writefds);

               if (fd1 > 0) {
                   FD_SET(fd1, &exceptfds);
                   nfds = max(nfds, fd1);
               }
               if (fd2 > 0) {
                   FD_SET(fd2, &exceptfds);
                   nfds = max(nfds, fd2);
               }

               ready = select(nfds + 1, &readfds, &writefds, &exceptfds, NULL);

               if (ready == -1 && errno == EINTR)
                   continue;

               if (ready == -1) {
                   perror("select()");
                   exit(EXIT_FAILURE);
               }

               if (FD_ISSET(h, &readfds)) {
                   socklen_t addrlen;
                   struct sockaddr_in client_addr;
                   int fd;

                   addrlen = sizeof(client_addr);
                   memset(&client_addr, 0, addrlen);
                   fd = accept(h, (struct sockaddr *) &client_addr, &addrlen);
                   if (fd == -1) {
                       perror("accept()");
                   } else {
                       SHUT_FD1;
                       SHUT_FD2;
                       buf1_avail = buf1_written = 0;
                       buf2_avail = buf2_written = 0;
                       fd1 = fd;
                       fd2 = connect_socket(forward_port, proxies[i].adress);
                       if (fd2 == -1)
                           SHUT_FD1;
                       else
                           printf("connect from %s\n",
                                  inet_ntoa(client_addr.sin_addr));

                       // Skip any events on the old, closed file
                         // descriptors. 

                       continue;
                   }
               }

               // NB: read OOB data before normal reads. 

               if (fd1 > 0 && FD_ISSET(fd1, &exceptfds)) {
                   char c;

                   nbytes = recv(fd1, &c, 1, MSG_OOB);
                   if (nbytes < 1)
                       SHUT_FD1;
                   else
                       send(fd2, &c, 1, MSG_OOB);
               }
               if (fd2 > 0 && FD_ISSET(fd2, &exceptfds)) {
                   char c;

                   nbytes = recv(fd2, &c, 1, MSG_OOB);
                   if (nbytes < 1)
                       SHUT_FD2;
                   else
                       send(fd1, &c, 1, MSG_OOB);
               }
               if (fd1 > 0 && FD_ISSET(fd1, &readfds)) {
                   nbytes = read(fd1, buf1 + buf1_avail,
                                 BUF_SIZE - buf1_avail);
                   if (nbytes < 1)
                       SHUT_FD1;
                   else
                       buf1_avail += nbytes;
               }
               if (fd2 > 0 && FD_ISSET(fd2, &readfds)) {
                   nbytes = read(fd2, buf2 + buf2_avail,
                                 BUF_SIZE - buf2_avail);
                   if (nbytes < 1)
                       SHUT_FD2;
                   else
                       buf2_avail += nbytes;
               }
               if (fd1 > 0 && FD_ISSET(fd1, &writefds) && buf2_avail > 0) {
                   nbytes = write(fd1, buf2 + buf2_written,
                                  buf2_avail - buf2_written);
                   if (nbytes < 1)
                       SHUT_FD1;
                   else
                       buf2_written += nbytes;
               }
               if (fd2 > 0 && FD_ISSET(fd2, &writefds) && buf1_avail > 0) {
                   nbytes = write(fd2, buf1 + buf1_written,
                                  buf1_avail - buf1_written);
                   if (nbytes < 1)
                       SHUT_FD2;
                   else
                       buf1_written += nbytes;
               }

               // Check if write data has caught read data. 

               if (buf1_written == buf1_avail)
                   buf1_written = buf1_avail = 0;
               if (buf2_written == buf2_avail)
                   buf2_written = buf2_avail = 0;

               // One side has closed the connection, keep
                 // writing to the other side until empty. 

               if (fd1 < 0 && buf1_avail - buf1_written == 0)
                   SHUT_FD2;
               if (fd2 < 0 && buf2_avail - buf2_written == 0)
                   SHUT_FD1;
           }
           exit(EXIT_SUCCESS);
	   }
	   else{
		pids[i] = pid;
	   }
       }
       printf("Press enter to finish\n");
       getchar();
       for(int i = 0; i < argc-1; i++){
	kill(pids[i],9);
       }
       free(pids);
       free(proxies);
}
