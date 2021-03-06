/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 *
 *  (C) 2013 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Defines INT32_MAX, which is not appropriate for int types. */
#include <stdint.h>

/* Defines INT_MAX */
#include <limits.h>

#include <mpi.h>

#include <assert.h>
static void MPIX_Verbose_abort(int errorcode)
{
    /* We do not check error codes here
     * because if MPI is in a really sorry state,
     * all of them might fail. */

    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    char errorstring[MPI_MAX_ERROR_STRING];
    memset(errorstring, 0, MPI_MAX_ERROR_STRING); /* optional */

    int errorclass;
    MPI_Error_class(errorcode, &errorclass);

    int resultlen;
    MPI_Error_string(errorcode, errorstring, &resultlen);

    fprintf(stderr, "%d: MPI failed (%d: %s) \n", rank, errorclass, errorstring);
    fflush(stderr); /* almost certainly redundant with the following... */

    MPI_Abort(MPI_COMM_WORLD, errorclass);

    return;
}
#define MPI_ASSERT(rc)  \
        ((void) ((rc==MPI_SUCCESS) ? 0 : MPIX_Verbose_abort(rc) ))

int MPIX_Type_contiguous_x(MPI_Count count, MPI_Datatype oldtype,
	MPI_Datatype * newtype);

#define BIGMPI_MAX INT_MAX

/*
 * Synopsis
 *
 * int MPIX_Type_contiguous_x(MPI_Count      count,
 *                            MPI_Datatype   oldtype,
 *                            MPI_Datatype * newtype)
 *                         
 *  Input Parameters
 *
 *   count             replication count (nonnegative integer)
 *   oldtype           old datatype (handle)
 *
 * Output Parameter
 *
 *   newtype           new datatype (handle)
 *
 */
int MPIX_Type_contiguous_x(MPI_Count count, MPI_Datatype oldtype, MPI_Datatype * newtype)
{
    MPI_Count c = count/BIGMPI_MAX;
    MPI_Count r = count%BIGMPI_MAX;

    MPI_Datatype chunk;
    MPI_ASSERT(MPI_Type_contiguous(BIGMPI_MAX, oldtype, &chunk));

    MPI_Datatype chunks;
    MPI_ASSERT(MPI_Type_contiguous(c, chunk, &chunks));

    MPI_Datatype remainder;
    MPI_ASSERT(MPI_Type_contiguous(r, oldtype, &remainder));

    int typesize;
    MPI_ASSERT(MPI_Type_size(oldtype, &typesize));

    MPI_Aint remdisp                   = (MPI_Aint)c*BIGMPI_MAX*typesize; /* must explicit-cast to avoid overflow */
    int array_of_blocklengths[2]       = {1,1};
    MPI_Aint array_of_displacements[2] = {0,remdisp};
    MPI_Datatype array_of_types[2]     = {chunks,remainder};

    MPI_ASSERT(MPI_Type_create_struct(2, array_of_blocklengths, array_of_displacements, array_of_types, newtype));
    MPI_ASSERT(MPI_Type_commit(newtype));

    MPI_ASSERT(MPI_Type_free(&chunk));
    MPI_ASSERT(MPI_Type_free(&chunks));
    MPI_ASSERT(MPI_Type_free(&remainder));

    return MPI_SUCCESS;
}


int main(int argc, char * argv[])
{
    int provided;
    size_t i;
    MPI_Count j;
    MPI_ASSERT(MPI_Init_thread(&argc, &argv, MPI_THREAD_SINGLE, &provided));

    int rank, size;
    MPI_ASSERT(MPI_Comm_rank(MPI_COMM_WORLD, &rank));
    MPI_ASSERT(MPI_Comm_size(MPI_COMM_WORLD, &size));

    int logn = (argc>1) ? atoi(argv[1]) : 32;
    size_t count = (size_t)1<<logn; /* explicit cast required */

    MPI_Datatype bigtype;
    MPI_ASSERT(MPIX_Type_contiguous_x( (MPI_Count)count, MPI_CHAR, &bigtype));
    MPI_ASSERT(MPI_Type_commit(&bigtype));

    MPI_Request requests[2];
    MPI_Status statuses[2];

    char * rbuf = NULL;
    char * sbuf = NULL;

    if (rank==(size-1)) {
        rbuf = malloc( count * sizeof(char)); assert(rbuf!=NULL);
        for (i=0; i<count; i++)
            rbuf[i] = 'a';

        MPI_ASSERT(MPI_Irecv(rbuf, 1, bigtype, 0,      0, MPI_COMM_WORLD, &(requests[1]) ));
    }
    if (rank==0) {
        sbuf = malloc( count * sizeof(char)); assert(sbuf!=NULL);
        for (i=0; i<count; i++)
            sbuf[i] = 'z';

        MPI_ASSERT(MPI_Isend(sbuf, 1, bigtype, size-1, 0, MPI_COMM_WORLD, &(requests[0]) ));
    }

    MPI_Count ocount[2];

    if (size==1) {
        MPI_ASSERT(MPI_Waitall(2, requests, statuses));
        MPI_ASSERT(MPI_Get_elements_x( &(statuses[1]), MPI_CHAR, &(ocount[1])));
    }
    else {
        if (rank==(size-1)) {
            MPI_ASSERT(MPI_Wait( &(requests[1]), &(statuses[1]) ));
            MPI_ASSERT(MPI_Get_elements_x( &(statuses[1]), MPI_CHAR, &(ocount[1]) ));
        } else if (rank==0) {
            MPI_ASSERT(MPI_Wait( &(requests[0]), &(statuses[0]) ));
            MPI_ASSERT(MPI_Get_elements_x( &(statuses[0]), MPI_CHAR, &(ocount[0]) ));
        }
    }

    /* correctness check */
    if (rank==(size-1)) {
        MPI_Count errors = 0;
        for (j=0; j<count; j++)
            errors += ( rbuf[j] != 'z' );
        if (errors == 0) {
	    printf(" No Errors\n");
	} else {
	    printf("errors = %lld \n", errors);
	}
    }

    free(rbuf);
    free(sbuf);

    MPI_ASSERT(MPI_Type_free(&bigtype));

    MPI_ASSERT(MPI_Finalize());

    return 0;
}
