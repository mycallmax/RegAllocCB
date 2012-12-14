/**************************************************************************
FILE: mcf.c (main for the mcf submission to SPEC)

AUTHOR: Andreas Loebel

    Konrad-Zuse-Zentrum fuer Informationstechnik Berlin (ZIB)
    Takustr. 7
    14195 Berlin-Dahlem

Copyright (c) 1998,1999   ZIB Berlin   All Rights Reserved
**************************************************************************/
/*  LAST EDIT: Thu Mar 18 11:04:24 1999 by Loebel/Borndoerfer (opt0)  */




#include "mcf.h"

#define REPORT

extern long min_impl_duration;
network_t net;





#ifdef _PROTO_
long global_opt( void )
#else
long global_opt( )
#endif
{
    long new_arcs;
    
    new_arcs = -1;

        
    while( new_arcs )
    {
#ifdef REPORT
        printf( "active arcs                : %ld\n", net.m );
#endif

        primal_net_simplex( &net );


#ifdef REPORT
        printf( "simplex iterations         : %ld\n", net.iterations );
        printf( "flow value                 : %0.0f\n", flow_cost(&net) );
#endif


        new_arcs = price_out_impl( &net );

#ifdef REPORT
        if( new_arcs )
            printf( "new implicit arcs          : %ld\n", new_arcs );
#endif
        
        if( new_arcs < 0 )
        {
#ifdef REPORT
            printf( "not enough memory, exit(-1)\n" );
#endif

            exit(-1);
        }

#ifndef REPORT
        printf( "\n" );
#endif

    }

    printf( "checksum                   : %ld\n", net.checksum );

    return 0;
}






#ifdef _PROTO_
int main( int argc, char *argv[] )
#else
int main( argc, argv )
    int argc;
    char *argv[];
#endif
{
    if( argc < 2 )
        return -1;


    printf( "\nMCF SPEC version 1.6.%s\n", ARITHMETIC_TYPE );
    printf( "by  Andreas Loebel\n" );
    printf( "Copyright (c) 1998,1999   ZIB Berlin\n" );
    printf( "All Rights Reserved.\n" );
    printf( "\n" );


    memset( (void *)(&net), 0, (size_t)sizeof(network_t) );
    net.bigM = (long)BIGM;

    strcpy( net.inputfile, argv[1] );
    
    if( read_min( &net ) )
    {
        printf( "read error, exit\n" );
        getfree( &net );
        return -1;
    }


#ifdef REPORT
    printf( "nodes                      : %ld\n", net.n_trips );
#endif


    primal_start_artificial( &net );
    global_opt( );

    /*
    suspend_impl( &net, (cost_t)(-1), 0 );
    */

#ifdef REPORT
    printf( "optimal\n" );
#endif

    

    if( write_circulations( "mcf.out", &net ) )
    {
        getfree( &net );
        return -1;    
    }


    getfree( &net );
    return 0;
}
