/*
 * This header defines MPI data types.
 */
#ifndef __MY_MPI_DATATYPE_H
#define __MY_MPI_DATATYPE_H

/*MPI equivalent data types for*/
enum _MPI_Datatype{
   MPI_CHAR,       //char
   MPI_INT,        //int
   MPI_DOUBLE      //double
};

typedef enum _MPI_Datatype MPI_Datatype;

extern char* mympi_datatypes[];


#endif
