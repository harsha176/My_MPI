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
    sizeof(char),		/*MPI_CHAR */
    sizeof(int),		/*MPI_INT */
    sizeof(double)		/*MPI_DOUBLE */
};

/** 
 * This function sends data to file descriptor 
 */
int __MPI_Send(int fd, void *buff, int length, MPI_Datatype datatype,
	       int tag)
{
    msg_t *pMsg;

    //create message header and add payload data
    if (create_data_msg
	(datatype, tag, buff, datatype_mappings[datatype] * length,
	 &pMsg) != MSG_SUCCESS) {
	dprintf("failed to create message\n");
	return MPI_ERR_OTHER;
    }
    dprintf("sending message\n");
    print_msg_hdr(pMsg);
    //send the data over file descriptor 
    if (send_msg(fd, pMsg) != MPI_SUCCESS) {
	return MPI_ERR_OTHER;
    }
    //cleanup
    free_data_msg(pMsg);
    return MPI_SUCCESS;
}

/**
 * This function receives data over file descriptor and updates status.
 */
int __MPI_Recv(int fd, int tag, MPI_Status * status, msg_t ** pMsg)
{
    //read message from file descriptor
    if (read_msg(fd, pMsg) != MSG_SUCCESS) {
	//update status length to 0
	if (status) {
	    status->length = 0;
	}
	return MPI_ERR_OTHER;
    }
    //update status to length size.
    if (status) {
	status->length += (*pMsg)->length;
    }

    return MPI_SUCCESS;
}

/**
 * This function parses MPI_Init arguments.
 *
 * Input parameters
 * 	pargc      pointer to number of arguments passed
 * 	pargv      pointer to arguments
 * Output parameters
 *      hostname         hostname of the processor
 *      root_hostname    hostname of the root processor
 *      root_port        root server port
 *      rank             rank of the processor
 *      nr_processor     Total number of processors
 * Return value
 * 	MPI_SUCCESS on successful parsing of the results or
 * 	else MPI_ERROR     
 */
int __parse_arguments(int *pargc, char ***pargv, char **hostname,
		      char **root_hostname, int *root_port, int *rank,
		      int *nr_processors)
{

    //check arguments
    if (!pargc || !pargv || !hostname || !root_hostname || !root_port
	|| !rank || !nr_processors) {
	dprintf("Invalid arguments to %s function\n", __func__);
	return MPI_ERR_OTHER;
    }

    int argc = *pargc;
    char **argv = *pargv;

    //check MPI arguments count
    if (argc != NR_ARGUMENTS) {
	dprintf("Invalid number of arguments:%d\n", argc);
	return MPI_ERR_OTHER;
    }
    //parse arguments
    *nr_processors = atoi(argv[NR_PROC_ARG_ID]);
    if (*nr_processors < 1) {
	dprintf("Invalid argument number of processors:%d\n",
		*nr_processors);
	return MPI_ERR_OTHER;
    }

    *rank = atoi(argv[RANK_ARG_ID]);
    if (*rank >= *nr_processors) {
	dprintf("Invalid argument rank of the processor:%d\n", *rank);
	return MPI_ERR_OTHER;
    }
    g_rank = *rank;

    *hostname = argv[HOSTNAME_ARG_ID];
    //copy hostname to buffer including NULL character
    strcpy(g_hostname, *hostname);

    *root_hostname = argv[ROOT_HOSTNAME_ARG_ID];

    *root_port = atoi(argv[ROOT_PORT_ARG_ID]);

    return MPI_SUCCESS;
}

/**
 * This function initializes global communicator object.
 *
 * Return value
 * 	MPI_SUCCESS if successful or else MPI_ERR_OTHER
 */
int __initialize_comm(int nr_processors, int rank)
{

    //allocate memory for one communicator
    if (commtab != NULL) {
	dprintf("Communicator table already initialized\n");
	return MPI_ERR_OTHER;
    }
    commtab = (struct _MPI_Comm *) malloc(sizeof(struct _MPI_Comm));
    if (!commtab) {
	dprintf("Failed to create communication table\n");
	return MPI_ERR_OTHER;
    }
    //initialize
    commtab->size = nr_processors;
    commtab->rank = rank;

    //allocate memory for context table
    commtab->ctable =
	(struct context_table *) malloc(sizeof(struct context_table) *
					nr_processors);
    if (!commtab->ctable) {
	dprintf("Failed to create context table\n");
	free(commtab);
	return MPI_ERR_OTHER;
    }
    memset(commtab->ctable, 0,
	   sizeof(struct context_table) * nr_processors);

    return MPI_SUCCESS;
}

/**
 * This function populates global communicator object for root.
 *
 * Input parameters
 * 		root_port 		root server port
 * Return value
 * 		MPI_SUCCESS on success else MPI_ERR_OTHER 
 */
int __populate_root_comm(int root_port, int nr_processors)
{

    /*start server wait for connections */
    struct sockaddr_in serv_addr;	//server address
    int sockfd;			//temporary socket descriptor
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
	dprintf("Failed to create server socket descriptor:%d\n",
		root_port);
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
    msg_t *pMsg;

    while (conn_count < (nr_processors - 1)) {
	newsockfd = accept(sockfd, (struct sockaddr *) NULL, 0);
	if (newsockfd < 0) {
	    dprintf("failed to accept connection\n");
	    continue;
	} else {
	    //get the rank of the process
	    if (__MPI_Recv(newsockfd, CONNECTION_TAG, &status, &pMsg)
		!= MPI_SUCCESS) {
		dprintf("Failed to read message for new connection\n");
		continue;
	    }
	    print_msg_hdr(pMsg);
	    if (pMsg->type != MSG_INIT) {
		dprintf("Expecting MSG_INIT message\n");
	    }

	    commtab->ctable[pMsg->init.rank].fd = newsockfd;
	    commtab->ctable[pMsg->init.rank].address = pMsg->init.address;
	    commtab->ctable[pMsg->init.rank].port = pMsg->init.port;
	    conn_count++;
	}
    }

    //clean up
    free(pMsg);

    return MPI_SUCCESS;
}

/** 
 * This function initializes communicator object for non root.
 *
 * Return value
 * 		MPI_SUCCESS on success or else MPI_ERR_OTHER
 */
int __populate_non_root_comm(int rank, char *root_hostname, int root_port)
{

    /*connect */
    int sockfd;
    struct sockaddr_in serv_addr;
    struct hostent *server;
    msg_t *pMsg;

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
    bcopy((char *) server->h_addr,
	  (char *) &serv_addr.sin_addr.s_addr, server->h_length);
    serv_addr.sin_port = htons(root_port);

    if ((connect
	 (sockfd, (struct sockaddr *) &serv_addr,
	  sizeof(serv_addr))) < 0) {
	dprintf("Failed to connect to server rank:%d\n", rank);
    }
    //create init message
    if (create_init_msg(rank, 0, &pMsg) != MPI_SUCCESS) {
	dprintf("Failed to create init message\n");
	return MPI_ERR_OTHER;
    }
    dprintf("Sending message\n");
    print_msg_hdr(pMsg);

    //send init message
    if (send_msg(sockfd, pMsg) != MPI_SUCCESS) {
	dprintf("Failed to send message\n");
	return MPI_ERR_OTHER;
    }
    //copy the socket descriptor
    commtab->ctable[ROOT].fd = sockfd;
    commtab->ctable[ROOT].address = 0;
    commtab->ctable[ROOT].port = root_port;

    //free init message
    free(pMsg);
    return MPI_SUCCESS;
}

/**
 * This function initializes MPI library.
 *
 */
int MPI_Init(int *pargc, char ***pargv)
{

    if (!pargc || !pargv) {
	return MPI_ERR_OTHER;
    }

    char *hostname;		//hostname of the processor
    char *root_hostname;	//hostname of the root
    int root_port;		//root port
    int rank;			//rank of this processor
    int nr_processors;		//nr_processors count


    //check if library is already initialized
    if (is_initialized) {
	return MPI_ERR_OTHER;
    }
    //parse arguments
    if (__parse_arguments
	(pargc, pargv, &hostname, &root_hostname, &root_port, &rank,
	 &nr_processors) != MPI_SUCCESS) {
	return MPI_ERR_OTHER;
    }
    //initialize global communicator object
    if (__initialize_comm(nr_processors, rank) != MPI_SUCCESS) {
	return MPI_ERR_OTHER;
    }
    //populate connection descriptors
    if (rank == ROOT) {
	if (__populate_root_comm(root_port, nr_processors) != MPI_SUCCESS) {
	    dprintf("Failed to populate communicator object for root\n");
	    return MPI_ERR_OTHER;
	}
    } else {
	if (__populate_non_root_comm(rank, root_hostname, root_port) !=
	    MPI_SUCCESS) {
	    dprintf
		("Failed to populate communicator object for non root processors");
	}
    }

    //set MPI library is intialized
    is_initialized = TRUE;

    return MPI_SUCCESS;
}

/**
 * This function returns size of the communicator.
 */
int MPI_Comm_size(MPI_Comm handle, int *size)
{
    if (!is_initialized) {
	return MPI_ERR_OTHER;
    }
    if (!size) {
	return MPI_ERR_OTHER;
    }
    //Ignoring handle parameter and return MPI_COMM_WORLD size 
    *size = commtab->size;
    return MPI_SUCCESS;
}

/**
 * This method retrieves rank of the current processor in 
 * the communicator.
 *
 * Input parameters
 * 	handle	   handle to communicator object
 * Output parameters
 * 	rank 	   rank of the processor
 * Return value
 *      MPI_SUCCESS if processor is part of communicator
 *      else MPI_ERROR 	
 */
int MPI_Comm_rank(MPI_Comm handle, int *rank)
{
    if (!is_initialized) {
	return MPI_ERR_OTHER;
    }

    if (!rank) {
	return MPI_ERR_OTHER;
    }
    //Ignoring handle parameters and return MPI_COMM_WORLD size 
    *rank = commtab->rank;
    return MPI_SUCCESS;
}


int MPI_Send(void *buff, int count, MPI_Datatype datatype,
	     int rank, int tag, MPI_Comm comm)
{
    if (!is_initialized) {
	return MPI_ERR_OTHER;
    }
    if (__MPI_Send(commtab->ctable[rank].fd, buff, count, datatype, tag) !=
	MPI_SUCCESS) {
	dprintf("failed send message\n");
	return MPI_ERR_OTHER;
    }

    return MPI_SUCCESS;
}

/**
 * This function determines ready descriptor.
 * Output parameters
 * 	    status 	    MPI_Status object updated with source
 * 	    ready_fd    ready file descriptor to read from
 * Return value
 * 	    MPI_SUCCESS on success or else MPI_ERROR_OTHER 
 */
int __get_receive_ready_descriptor(MPI_Status * status, int *ready_fd)
{

    //check arguments
    if (!status || !ready_fd) {
	dprintf("Invalid arguments to %s\n", __func__);
	return MPI_ERR_OTHER;
    }

    /*multiplex io on all descriptors */
    fd_set rset;

    /*initialize the set: all bits off */
    FD_ZERO(&rset);

    //loop and add all valid descriptors in communicator
    int i = 0;
    int maxfpd = 0;
    for (i = 0; i < commtab->size; i++) {
	if (commtab->ctable[i].fd) {
	    FD_SET(commtab->ctable[i].fd, &rset);
	    if (maxfpd < commtab->ctable[i].fd) {
		maxfpd = commtab->ctable[i].fd;
	    }
	}
    }

    //wait for any of the ready objects to be ready
    if (select(maxfpd + 1, &rset, NULL, NULL, NULL) < 0) {
	dprintf("Failed to receive message from any of the source\n");
	return MPI_ERR_OTHER;
    }
    //initialize ready descriptor
    *ready_fd = -1;

    //find the ready descriptor and update processor associated with it in 
    //status
    for (i = 0; i < commtab->size; i++) {
	if (commtab->ctable[i].fd) {
	    if (FD_ISSET(commtab->ctable[i].fd, &rset)) {
		*ready_fd = commtab->ctable[i].fd;
		status->MPI_SOURCE = i;
		dprintf
		    ("descriptor ready on connection rank:%d and fd:%d\n",
		     i, *ready_fd);
	    }
	}
    }

    dprintf("Waiting to receive message from src rank:%d dst rank:%d\n",
	    status->MPI_SOURCE, g_rank);

    //check the validity of the descriptor
    if (*ready_fd == -1) {
	dprintf("Failed to receive message from spurious source\n");
	return MPI_ERR_OTHER;
    }

    return MPI_SUCCESS;
}


int MPI_Recv(void *buff, int count, MPI_Datatype datatype,
	     int rank, int tag, MPI_Comm comm, MPI_Status * status)
{
    if (!is_initialized) {
	return MPI_ERR_OTHER;
    }
    //initialize status object
    memset(status, 0, sizeof(MPI_Status));
    msg_t *pMsg;

    //identify ready descriptor
    int ready_fd;
    if (__get_receive_ready_descriptor(status, &ready_fd) != MPI_SUCCESS) {
	dprintf("Failed to identify sending processor node\n");
	return MPI_ERR_OTHER;
    }
    //receive the message
    if (__MPI_Recv(ready_fd, tag, status, &pMsg) != MPI_SUCCESS) {
	dprintf("Failed to send message to root\n");
	return MPI_ERR_OTHER;
    }
    //copy data to buffer
    memcpy(buff, pMsg->payload, pMsg->length);

    free(pMsg);
    return MPI_SUCCESS;
}


int MPI_Get_count(MPI_Status * status, MPI_Datatype datatype, int *count)
{
    if (!is_initialized) {
	return MPI_ERR_OTHER;
    }

    if (!status) {
	return MPI_ERR_OTHER;
    }

    *count = status->length / datatype_mappings[datatype];
    return MPI_SUCCESS;
}


int MPI_Finalize(void)
{
    if (!is_initialized) {
	return MPI_ERR_OTHER;
    }

    //non root users block till root says exit
    if (g_rank != ROOT) {
	char buf;
	while (read(commtab->ctable[ROOT].fd, &buf, sizeof(char)) < 0) {
	    ;
	}
    }
    //close all connection descriptors
    struct context_table *ctable = commtab->ctable;
    int i;
    for (i = 0; i < commtab->size; i++) {
	if (ctable[i].fd) {
	    close(ctable[i].fd);
	}
    }

    //free memory
    if (ctable) {
	free(ctable);
    }
    if (commtab) {
	free(commtab);
    }
    return MPI_SUCCESS;
}


int MPI_Get_processor_name(char *name, int *len)
{
    if (!is_initialized) {
	return MPI_ERR_OTHER;
    }

    if (!name || !len) {
	return MPI_ERR_OTHER;
    }

    strcpy(name, g_hostname);
    return MPI_SUCCESS;
}
