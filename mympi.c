#include "mympi.h"
#include "mymsg.h"
#include "debug.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/*Define boolean values*/
#define FALSE              0
#define TRUE               1

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

/**
 * Datatype size mappings from MPI_Datatype to C datatypes
 */
int datatype_mappings[] = {
    sizeof(char),  /*MPI_CHAR*/
    sizeof(int),   /*MPI_INT*/
    sizeof(double) /*MPI_DOUBLE*/
};

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
	if ((nread = read(fd, ptr, nleft) < 0)) {
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
	if (!status) {
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
    //send the data over file descriptor 
    if (writen(fd, buff, MSG_SIZE(pMsg->length)) != MSG_SIZE(pMsg->length)) {
	dprintf("Failed to write data of size:%lu \n",
		MSG_SIZE(pMsg->length));
	return MPI_ERR_OTHER;
    }
    //cleanup
    free_data_msg(pMsg);
    return MPI_SUCCESS;
}


int __MPI_Recv(int fd, MPI_Datatype datatype, int rank,
	       int tag, MPI_Status* status, msg_t** pMsg)
{

    int msg_length;		//message payload length
    //read length of the message
    if (read(fd, &msg_length, LENGTH_SIZE) != LENGTH_SIZE) {
	dprintf("Failed to read length \n", __func__, __LINE__);
	return MPI_ERR_OTHER;
    }
    //allocate memory for local buffer 
    char *local_buffer = (char *) malloc(MSG_SIZE(msg_length));
    if (!local_buffer) {
	dprintf
	    ("Failed to allocate memory for local buffer of size %lu \n",
	     MSG_SIZE(msg_length), __func__, __LINE__);
	return MPI_ERR_OTHER;
    }
    memcpy(local_buffer, &msg_length, LENGTH_SIZE);

    //read entire message from the buffer
    if (readn
	(fd, local_buffer + LENGTH_SIZE,
	 MSG_SIZE(msg_length - LENGTH_SIZE), status) != (MSG_SIZE(msg_length - LENGTH_SIZE))) {
	dprintf("failed to read %ld bytes\n", msg_length);
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
    if(nr_processors < 1) {
       dprintf("Invalid argument number of processors:%d\n", nr_processors);
       return MPI_ERR_OTHER;
    }
  
    rank = atoi(argv[RANK_ARG_ID]);
    if(rank >= nr_processors) {
      dprintf("Invalid argument rank of the processor:%d\n", rank);
      return MPI_ERR_OTHER;
    }
 
    hostname = argv[HOSTNAME_ARG_ID];
    
    root_hostname = argv[ROOT_HOSTNAME_ARG_ID];
 
    root_port = atoi(argv[ROOT_PORT_ARG_ID]);

    //allocate memory for one communicator
    if(commtab != NULL) {
      dprintf("Communicator table already initialized\n");
      return MPI_ERR_OTHER;  
    }
    commtab = (struct _MPI_Comm*)malloc(sizeof(MPI_Comm));
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
    memset(&commtab->ctable, 0, sizeof(struct context_table)*nr_processors); 

    //populate connection descriptors
    if(rank == ROOT) {
         /*start server wait for connections*/
    } else {
         /*connect*/
    }
}


int MPI_Comm_size(MPI_Comm handle, int *size)
{
    return MPI_ERR_OTHER;
}


int MPI_Comm_rank(MPI_Comm handle, int *rank)
{
    return MPI_ERR_OTHER;
}


int MPI_Send(void *buff, int count, MPI_Datatype datatype,
	     int rank, int tag, MPI_Comm comm)
{
    return MPI_ERR_OTHER;
}


int MPI_Recv(void *buff, int count, MPI_Datatype datatype,
	     int rank, int tag, MPI_Comm comm, MPI_Status * status)
{
    return MPI_ERR_OTHER;
}


int MPI_Get_count(MPI_Status * status, MPI_Datatype datatype, int *count)
{
    return MPI_ERR_OTHER;
}


int MPI_Finalize(void)
{
    return MPI_SUCCESS;
}


int MPI_Get_processor_name(char *name, int *len)
{
    return MPI_ERR_OTHER;
}
