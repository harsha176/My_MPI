#include "mympi.h"
#include "mymsg.h"
#include "debug.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include <sys/time.h>
#include <sys/select.h>

/*Define boolean values*/
#define FALSE              0
#define TRUE               1

/*Pending connections queue length*/
#define PENDING_CONNECTIONS_QUEUE_LENGTH 100

/*Dummy tag used while establishing connection*/
#define CONNECTION_TAG          0

/*MPI arguments */
/*
 * 1. number of processors
 * 2. rank of this processor
 * 3. hostname of this processor
 * 4. hostname of the root processor
 * 5. root port of the host processor
 */ 
#define NR_PROC_ARG_ID       1
#define RANK_ARG_ID          2
#define HOSTNAME_ARG_ID      3
#define ROOT_HOSTNAME_ARG_ID 4
#define ROOT_PORT_ARG_ID     5

#define NR_ARGUMENTS         6

/*Root processor rank*/
#define ROOT		     0

/*Initialization flag*/
static int is_initialized = FALSE;

/*Communicator table*/
struct _MPI_Comm *commtab = NULL;

/*global hostname for MPI_Get_Processor name*/
char g_hostname[MPI_MAX_PROCESSOR_NAME];

/*global rank*/
int g_rank;

/**
 * Datatype size mappings from MPI_Datatype to C datatypes
 */
int datatype_mappings[] = {
    sizeof(char),  /*MPI_CHAR*/
    sizeof(int),   /*MPI_INT*/
    sizeof(double) /*MPI_DOUBLE*/
};

int __send_msg(int fd, void* buff, int length);

/**
 * This function will take care of sending data of size n bytes
 * over fd.
 *
 * Lifter from stevens UNP_Vol_1
 */
int readn(int fd, void *vptr, unsigned int n, MPI_Status * status)
{
    unsigned int nleft;
    unsigned int nread;
    char *ptr;

    ptr = vptr;
    nleft = n;

    while (nleft > 0) {
	if ((nread = read(fd, ptr, nleft)) < 0) {
	    if (errno == EINTR) {
		nread = 0;
	    } else {
		return MPI_ERR_OTHER;
	    }
	} else if (nread == 0) {
	    break;
	}
	nleft -= nread;
	ptr += nread;
	if (status) {
	    status->length += nread;
	}
    }
    return (n - nleft);
}


/*
 * This function will take care of recieving data of size n bytes
 * over fd.
 *
 * Lifted from stevens UNP_Vol_1
 */
int writen(int fd, const void *vptr, unsigned int n)
{
    unsigned int nleft;
    unsigned int nwritten;
    const char *ptr;

    ptr = vptr;
    nleft = n;

    while (nleft > 0) {
	if ((nwritten = write(fd, ptr, nleft)) <= 0) {
	    if (nwritten < 0 && errno == EINTR)
		nwritten = 0;
	    else
		return MPI_ERR_OTHER;
	}

	nleft -= nwritten;
	ptr += nwritten;
    }

    return (n);
}

/** 
 * This code sends data to file descriptor 
 */
int __MPI_Send(int fd, void *buff, int length, MPI_Datatype datatype,
	       int tag)
{
    msg_t *pMsg;

    //create message header and add payload data
    if (create_data_msg
	(datatype, tag, buff, datatype_mappings[datatype]*length,
	 &pMsg) != MSG_SUCCESS) {
	dprintf("failed to create message\n");
	return MPI_ERR_OTHER;
    }
    dprintf("sending message\n");
    print_msg_hdr(pMsg);
    //send the data over file descriptor 
    if(__send_msg(fd, pMsg, MSG_SIZE(pMsg->length)) != MPI_SUCCESS) {
       return MPI_ERR_OTHER;
    }

    //cleanup
    free_data_msg(pMsg);
    return MPI_SUCCESS;
}

int __send_msg(int fd, void* buff, int length) {
    if (writen(fd, buff, length) != length){
	dprintf("Failed to write data of size:%u \n",
		length);
	return MPI_ERR_OTHER;
    }
    
    return MPI_SUCCESS; 
}

int __MPI_Recv(int fd, int tag, MPI_Status* status, msg_t** pMsg)
{

    int msg_length;		//message payload length
    //read length of the message
    if (read(fd, &msg_length, LENGTH_SIZE) != LENGTH_SIZE) {
	dprintf("Failed to read length \n");
	return MPI_ERR_OTHER;
    }
    //update read count
    if(status) {
       status->length += 4;
    }
    //allocate memory for local buffer 
    char *local_buffer = (char *) malloc(MSG_SIZE(msg_length));
    if (!local_buffer) {
	dprintf
	    ("Failed to allocate memory for local buffer of size %lu \n",
	     MSG_SIZE(msg_length));
	return MPI_ERR_OTHER;
    }
    memcpy(local_buffer, &msg_length, LENGTH_SIZE);

    //read entire message from the buffer
    if (readn
	(fd, local_buffer + LENGTH_SIZE,
	 MSG_SIZE(msg_length - LENGTH_SIZE), status) != (MSG_SIZE(msg_length - LENGTH_SIZE))) {
	dprintf("failed to read %d bytes\n", msg_length);
	return MPI_ERR_OTHER;
    }
    //parse the buffer into message
    if (parse_msg(local_buffer, msg_length, pMsg) != MSG_SUCCESS) {
	dprintf("failed to parse message\n");
	return MPI_ERR_OTHER;
    }

    return MPI_SUCCESS;
}

int MPI_Init(int *pargc, char ***pargv) {
   
   if(!pargc || !pargv) {
      return MPI_ERR_OTHER; 
   }
   
   int    argc;
   char** argv;
   char*  hostname;       //hostname of the processor
   char*  root_hostname;  //hostname of the root
   int    root_port;      //root port
   int    rank;           //rank of this processor
   int    nr_processors;  //nr_processors count

   argc = *pargc;
   argv = *pargv;

   //check if library is already initialized
   if(is_initialized) {
      return MPI_ERR_OTHER;
   }
  
   if(argc != NR_ARGUMENTS) {
      dprintf("Invalid number of arguments:%d\n", argc);
      return MPI_ERR_OTHER;
   } 

   //parse arguments
    nr_processors = atoi(argv[NR_PROC_ARG_ID]);
    if(nr_processors < 1) {
       dprintf("Invalid argument number of processors:%d\n", nr_processors);
       return MPI_ERR_OTHER;
    }
  
    rank = atoi(argv[RANK_ARG_ID]);
    if(rank >= nr_processors) {
      dprintf("Invalid argument rank of the processor:%d\n", rank);
      return MPI_ERR_OTHER;
    }
    g_rank = rank;
 
    hostname = argv[HOSTNAME_ARG_ID];
    //copy hostname to buffer including NULL character
    strcpy(g_hostname, hostname);

    root_hostname = argv[ROOT_HOSTNAME_ARG_ID];

 
    root_port = atoi(argv[ROOT_PORT_ARG_ID]);

    //allocate memory for one communicator
    if(commtab != NULL) {
      dprintf("Communicator table already initialized\n");
      return MPI_ERR_OTHER;  
    }
    commtab = (struct _MPI_Comm*)malloc(sizeof(struct _MPI_Comm));
    if(!commtab) {
       dprintf("Failed to create communication table\n");
       return MPI_ERR_OTHER;
    }
    //initialize
    commtab->size = nr_processors;
    commtab->rank = rank;
    //allocate memory for context table
    commtab->ctable = (struct context_table*)malloc(sizeof(struct context_table)*nr_processors);
    if(!commtab->ctable)  {
       dprintf("Failed to create context table\n");
       free(commtab);
       return MPI_ERR_OTHER;
    }
    memset(commtab->ctable, 0, sizeof(struct context_table)*nr_processors); 

    //populate connection descriptors
    if(rank == ROOT) {
         /*start server wait for connections*/
         struct sockaddr_in serv_addr;  //server address
         int sockfd;                    //temporary socket descriptor
         sockfd = socket(AF_INET, SOCK_STREAM, 0);
         if (sockfd < 0) {
             dprintf("Failed to create server socket descriptor:%d\n", root_port);
             return MPI_ERR_OTHER;
         }
         memset((char *) &serv_addr, 0, sizeof(serv_addr));
         serv_addr.sin_family = AF_INET;
         serv_addr.sin_addr.s_addr = INADDR_ANY;
         serv_addr.sin_port = htons(root_port);

         if (bind(sockfd, (struct sockaddr *) &serv_addr,
                sizeof(serv_addr)) < 0) {
             dprintf("Failed to bind to server port:%d\n", root_port);
             return MPI_ERR_OTHER;
         }

         listen(sockfd, PENDING_CONNECTIONS_QUEUE_LENGTH);

         //accept connections from all the other nodes
         int conn_count = 0;
         int newsockfd;
         MPI_Status status;
         msg_t* pMsg;
         while(conn_count < (nr_processors - 1)) {
            newsockfd = accept(sockfd, (struct sockaddr *) NULL, 0);
            if(newsockfd < 0) { 
               dprintf("failed to accept connection\n");
               continue;
            } else {
               //get the rank of the process
               if(__MPI_Recv(newsockfd, CONNECTION_TAG, &status, &pMsg) 
			!= MPI_SUCCESS) {
                  dprintf("Failed to read message for new connection\n");
                  continue;
               }
               print_msg_hdr(pMsg);
               if(pMsg->type != MSG_INIT) {
                  dprintf("Expecting MSG_INIT message\n");
               }
	       commtab->ctable[pMsg->init.rank].fd = newsockfd;
               commtab->ctable[pMsg->init.rank].address = pMsg->init.address;
               commtab->ctable[pMsg->init.rank].port = pMsg->init.port;
               conn_count++;
            }
         }
    } else {
         /*connect*/
         int sockfd;
         struct sockaddr_in serv_addr;
         struct hostent *server;
         msg_t* pMsg;

         sockfd = socket(AF_INET, SOCK_STREAM, 0);
          
         if (sockfd < 0) {
            dprintf("Failed to opening socket rank:%d\n", rank);
            return MPI_ERR_OTHER;
         }
         server = gethostbyname(root_hostname);
         if (server == NULL) {
            dprintf("No such host\n");
            return MPI_ERR_OTHER;
         }

         memset((char *) &serv_addr, 0, sizeof(serv_addr));
         serv_addr.sin_family = AF_INET;
         bcopy((char *)server->h_addr, 
         (char *)&serv_addr.sin_addr.s_addr,
         server->h_length);
         serv_addr.sin_port = htons(root_port);
         if ((connect(sockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr))) < 0) {
            dprintf("Failed to connect to server rank:%d\n", rank);
         }
         //create init message
         if(create_init_msg(rank, 0, &pMsg) != MPI_SUCCESS) {
            dprintf("Failed to create init message\n");
            return MPI_ERR_OTHER;
         }
         dprintf("Sending message\n");
         print_msg_hdr(pMsg);
         //send init message
         if(__send_msg(sockfd, pMsg, MSG_SIZE(pMsg->length)) != MPI_SUCCESS) {
            dprintf("Failed to send message\n");
            return MPI_ERR_OTHER;
         }  

         //copy the socket descriptor
	 commtab->ctable[ROOT].fd = sockfd;
         commtab->ctable[ROOT].address = 0;
         commtab->ctable[ROOT].port = root_port;
    }
    is_initialized = TRUE;
    return MPI_SUCCESS;
}

/**
 * This function returns size of the communicator.
 */
int MPI_Comm_size(MPI_Comm handle, int *size) {
    if(!is_initialized) {
       return MPI_ERR_OTHER;
    }
    if(!size) {
       return MPI_ERR_OTHER;
    }

    //Ignoring handle parameter and return MPI_COMM_WORLD size 
    *size = commtab->size;
    return MPI_SUCCESS;
}


int MPI_Comm_rank(MPI_Comm handle, int *rank) {
    if(!is_initialized) {
       return MPI_ERR_OTHER;
    }

    if(!rank) {
       return MPI_ERR_OTHER;
    }

    //Ignoring handle parameters and return MPI_COMM_WORLD size 
    *rank = commtab->rank;
    return MPI_SUCCESS;
}


int MPI_Send(void *buff, int count, MPI_Datatype datatype,
	     int rank, int tag, MPI_Comm comm) {
    if(!is_initialized) {
       return MPI_ERR_OTHER;
    }
    if(__MPI_Send(commtab->ctable[rank].fd, buff, count, datatype, tag) != MPI_SUCCESS) {
      dprintf("failed send message\n");
      return MPI_ERR_OTHER;
    }

    return MPI_SUCCESS;
}


int MPI_Recv(void *buff, int count, MPI_Datatype datatype,
	     int rank, int tag, MPI_Comm comm, MPI_Status * status) {
    if(!is_initialized) {
       return MPI_ERR_OTHER;
    }
    
    memset(status, 0, sizeof(MPI_Status));
    //remove the length of msg header
    status->length += -MIN_MSG_LENGTH;
    msg_t* pMsg;

    /*multiplex io on all descriptors*/
    fd_set rset;
     
    FD_ZERO(&rset);     /*initialize the set: all bits off*/
    //loop and add it to rset
    int i = 0;
    int maxfpd = 0;
    for (i = 0; i < commtab->size; i++) {
      if(commtab->ctable[i].fd) {
         FD_SET(commtab->ctable[i].fd, &rset);
         if(maxfpd < commtab->ctable[i].fd) {
             maxfpd = commtab->ctable[i].fd;
         }
      }
    }
  
    if(select(maxfpd + 1, &rset, NULL, NULL, NULL) < 0 ) {
       dprintf("Failed to receive message from any of the source\n");
       return MPI_ERR_OTHER;
    }

    int ready_fd = -1;
    for (i = 0; i < commtab->size; i++) {
      if(commtab->ctable[i].fd) {
         if(FD_ISSET(commtab->ctable[i].fd, &rset)) {
           ready_fd = commtab->ctable[i].fd;
           status->MPI_SOURCE = i;
           dprintf("descriptor ready on connection rank:%d and fd:%d\n", i, ready_fd);
         }
      }
    }

    dprintf("Waiting to receive message from src rank:%d dst rank:%d\n", status->MPI_SOURCE, g_rank);

    if(ready_fd == -1) {
       dprintf("Failed to receive message from spurious source\n");
       return MPI_ERR_OTHER;
    }
    if(__MPI_Recv(ready_fd, tag, status, &pMsg) != MPI_SUCCESS) {
       dprintf("Failed to send message to root\n");
    }

    return MPI_SUCCESS;
}


int MPI_Get_count(MPI_Status* status, MPI_Datatype datatype, int *count)
{
    if(!is_initialized) {
        return MPI_ERR_OTHER;
    }
  
    if(!status) {
        return MPI_ERR_OTHER;
    }

    *count = status->length/datatype_mappings[datatype];
    return MPI_SUCCESS;
}


int MPI_Finalize(void)
{
    if(!is_initialized) {
       return MPI_ERR_OTHER;
    }
   
    //close all connection descriptors
    struct context_table* ctable = commtab->ctable;
    int i; 

    //non root users block till root says exit
    if(g_rank != ROOT) {
       char buf;
       while(read(commtab->ctable[ROOT].fd, &buf, sizeof(char)) < 0) {
          ;
       }
    }

    for(i = 0; i < commtab->size; i++) {
          if(ctable[i].fd) {
             close(ctable[i].fd);
          }
    }
   
    //free memory
    if(ctable) {
      free(ctable);
    }
    if(commtab) {
      free(commtab);
    }
    return MPI_SUCCESS;
}


int MPI_Get_processor_name(char *name, int *len)
{
    if(!is_initialized) {
        return MPI_ERR_OTHER;
    }

    if(!name || !len) {
        return MPI_ERR_OTHER;
    }

    strcpy(name, g_hostname);
    return MPI_SUCCESS;
}
