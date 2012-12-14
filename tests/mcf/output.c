/**************************************************************************
FILE: output.c

AUTHOR: Andreas Loebel

    Konrad-Zuse-Zentrum fuer Informationstechnik Berlin (ZIB)
    Takustr. 7
    14195 Berlin-Dahlem

Copyright (c) 1998,1999   ZIB Berlin   All Rights Reserved
**************************************************************************/
/*  LAST EDIT: Mon Feb 22 17:41:42 1999 by Andreas Loebel (opt0)  */



#include "output.h"





#ifdef _PROTO_
long write_circulations(
                   char *outfile,
                   network_t *net
                   )
#else
long write_circulations( outfile, net )
     char *outfile;
     network_t *net;
#endif 

{
    FILE *out = NULL;
    arc_t *block;
    arc_t *arc;
    arc_t *arc2;
    arc_t *first_impl = net->stop_arcs - net->m_impl;

    if(( out = fopen( outfile, "w" )) == NULL )
        return -1;

    refresh_neighbour_lists( net );
    
    for( block = net->nodes[net->n].firstout; block; block = block->nextout )
    {
        if( block->flow )
        {
            fprintf( out, "()\n" );
            
            arc = block;
            while( arc )
            {
                if( arc >= first_impl )
                    fprintf( out, "***\n" );

                fprintf( out, "%ld\n", - arc->head->number );
                arc2 = arc->head[net->n_trips].firstout; 
                for( ; arc2; arc2 = arc2->nextout )
                    if( arc2->flow )
                        break;
                if( !arc2 )
                {
                    fclose( out );
                    return -1;
                }
                
                if( arc2->head->number )
                    arc = arc2;
                else
                    arc = NULL;
            }
        }
    }
    


    fclose(out);
    
    return 0;
}
