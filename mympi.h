/**
 * This header defines custom MPI library implementation for HW2 Problem 1.
 * It reflects the same interface of MPI API
 */
#ifndef MY_MPI_H
#define MY_MPI_H

#include "mympidatatype.h"
#include <stdint.h>
#include <stdbool.h>
#include <time.h>   //for MPI_Wtime

/*MPI Return values*/
#define MPI_SUCCESS    0       //No error; MPI routine completed successfully    
#define MPI_ERR_OTHER -1       //MPI_INIT initialization error
#define MPI_ERR_COUNT -2       //Invalid count argument. Count arguments must be
			       //non-negative; a count of zero is often valid.
#define MPI_ERR_TYPE  -3       //Invalid datatype argument. May be an 
                               //uncommitted MPI_Datatype (see MPI_Type_commit).
#define MPI_ERR_TAG   -4       //Invalid tag argument. Tags must be non-negative
                               //; tags in a receive (MPI_Recv, MPI_Irecv,
                               //MPI_Sendrecv, etc.) may also be MPI_ANY_TAG. 
                               //The largest tag value is available through 
                               //the the attribute MPI_TAG_UB
#define MPI_ERR_RANK  -5       //Invalid source or destination rank. Ranks 
                               //must be between zero and the size of the 
                               //communicator minus one; ranks in a receive 
                               //(MPI_Recv, MPI_Irecv, MPI_Sendrecv, etc.) 
                               //may also be MPI_ANY_SOURCE.



/*MPI TAG Constants*/
#define MPI_ANY_SOURCE 0
#define MPI_ANY_TAG    1

static inline double MPI_Wtime() {
  ((double)clock()/CLOCKS_PER_SEC);
}

/*Maximum size of processor name*/
#define MPI_MAX_PROCESSOR_NAME 256

/*MPI_Communicator*/
typedef int MPI_Comm;


/*Status definition*/
struct _MPI_Status {
   int MPI_SOURCE;   //Source of the message
   int length;	     //length of message received so far
};

typedef struct _MPI_Status MPI_Status;

/*Context table definition*/
struct context_table {
   int fd;             //connection file descriptor
   unsigned int rank;  //rank of the other processor
   uint32_t address;   //ip address in host byte order 
   uint16_t port;      //port address in host byte order
};

/*MY MPI Comm*/
struct _MPI_Comm {
   unsigned int size;             //size of the communicator
   unsigned int rank;             //rank of the processor in communicator
   struct context_table* ctable;  //array of entries in context table
};


/*MPI_Comm Constants*/
#define MPI_COMM_WORLD 0

/**
 * Initialize the MPI execution environment
 * Input parameters
 * 	argc: Pointer to the number of arguments
 * 	argv: Pointer to the argument vector
 * Return value
 * 	MPI_SUCCESS on succesfull initialization of MPI Library
 * 	MPI_ERR_OTHER if library is already initialized.	
 */
int MPI_Init(int * /*argc*/, char *** /*argv*/);

/**
 * Determines the size of the group associated with a communicator.
 * Input parameters
 * 	communicator handle   This parameter is ignored and MPI_COMM_WORLD 
 * 	                      will be used. All the communication in rtt.c 
 * 	                      is within MPI_COMM_WORLD
 * Output parameters
 * 	size: 	              Number of processes in the group of comm (integer)  
 * Return value
 * 	MPI_SUCCESS	
 */
int MPI_Comm_size(MPI_Comm /*handle*/, int * /*size*/);

/**
 * Determines the rank of the calling process in the communicator.
 * Input parameters
 * communicator (handle):    This parameter is ignored and MPI_COMM_WORLD 
 * 			     will be used
 * Output parameters
 * rank: Rank of the calling process in the group of comm (integer)	
 */
int MPI_Comm_rank(MPI_Comm /*handle*/, int * /*rank*/);

/**
 * Gets the name of the processor
 * Output Parameters
 *
 * name  A unique specifier for the actual (as opposed to virtual) node. This must be an array of size at least MPI_MAX_PROCESSOR_NAME.
 * resultlen  Length (in characters) of the name
 */
int MPI_Get_processor_name(char *, int *);

/** 
 *  Performs a blocking send
 *
 *  Input Parameters
 *  buf:   initial address of send buffer (choice)
 *  count:   number of elements in send buffer (nonnegative integer)
 *  datatype:   datatype of each send buffer element (handle)
 *  dest:   rank of destination (integer)
 *  tag:   message tag (integer)
 *  comm:  communicator (handle)
 *
 */
int MPI_Send(void* /*buff*/, int /*count*/, MPI_Datatype /*datatype*/, 
             int /*rank*/, int /*tag*/, MPI_Comm /*comm*/);

/**
 * Blocking receive for a message
 *
 * Input Parameters
 * count  maximum number of elements in receive buffer (integer)
 * datatype  datatype of each receive buffer element (handle)
 * source  rank of source (integer)
 * tag  message tag (integer)
 * comm  communicator (handle)
 *
 * Output Parameters
 * buf initial address of receive buffer (choice)
 * status status object (Status)
 */
int MPI_Recv(void* /*buff*/, int /*count*/, MPI_Datatype /*datatype*/, 
             int /*rank*/, int /*tag*/, MPI_Comm /*comm*/, 
             MPI_Status * /*status*/);

/**
 * Gets the number of "top level" elements
 *
 * Input Parameters
 * status   return status of receive operation (Status)
 * datatype datatype of each receive buffer element (handle)
 *
 * Output Parameter
 * count  number of received elements (integer) Notes: 
 * If the size of the datatype is zero, this routine will return a count of zero. 
 * If the amount of data in status is not an exact multiple of the size of datatype 
 * (so that count would not be integral), a count of MPI_UNDEFINED is returned instead.
 *
 * Return value
 * MPI_SUCCESS   No error; MPI routine completed successfully.
 * MPI_ERR_TYPE  Invalid datatype argument. May be an uncommitted MPI_Datatype (see MPI_Type_commit).
 *
 */
int MPI_Get_count(MPI_Status * /*status*/, MPI_Datatype /*datatype*/, int * /*count*/);

/**
 * Terminates MPI execution environment
 *
 * Return value
 * MPI_SUCCESS
 */
int MPI_Finalize(void);

#endif
