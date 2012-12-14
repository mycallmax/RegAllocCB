/**************************************************************************
FILE: readmin.c

AUTHOR: Andreas Loebel

    Konrad-Zuse-Zentrum fuer Informationstechnik Berlin (ZIB)
    Takustr. 7
    14195 Berlin-Dahlem

Copyright (c) 1998,1999   ZIB Berlin   All Rights Reserved
**************************************************************************/
/*  LAST EDIT: Thu Mar 18 10:15:30 1999 by Andreas Loebel (opt0)  */


#define SPEC_STATIC


#include "readmin.h"




#ifdef _PROTO_
long read_min( network_t *net )
#else
long read_min( net )
     network_t *net;
#endif
{                                       
    FILE *in = NULL;
    char instring[201];
    long t, h, c;
    long i;
    arc_t *arc;
    node_t *node;


    if(( in = fopen( net->inputfile, "r")) == NULL )
        return -1;

    fgets( instring, 200, in );
    if( sscanf( instring, "%ld %ld", &t, &h ) != 2 )
        return -1;
    

    net->n_trips = t;
    net->m_org = h;
    net->n = (t+t+1); 
    net->m = (t+t+t+h);
#ifdef SPEC_STATIC
    net->max_m = 0x2e0000l;
#else
    net->max_m = MAX( net->m + MAX_NEW_ARCS, STRECHT(STRECHT(net->m)) );
#endif

    
    net->nodes      = (node_t *) calloc( net->n + 1, sizeof(node_t) );
    net->dummy_arcs = (arc_t *)  calloc( net->n,   sizeof(arc_t) );
    net->arcs       = (arc_t *)  calloc( net->max_m,   sizeof(arc_t) );

    if( !( net->nodes && net->arcs && net->dummy_arcs ) )
    {
        printf( "read_min(): not enough memory\n" );
        getfree( net );
        return -1;
    }

    /**
    printf( "alloc for nodes       MB %3d\n", 
            (long)((net->n + 1)*sizeof(node_t) / 0x100000) );
    printf( "alloc for dummy arcs  MB %3d\n", 
            (long)((net->n)*sizeof(arc_t) / 0x100000) );
    printf( "alloc for arcs        MB %3d\n", 
            (long)((net->max_m)*sizeof(arc_t) / 0x100000) );
    printf( "----------------------------\n" );
    printf( "heap about            MB %3d\n\n", 
            (long)((net->max_m)*sizeof(arc_t) / 0x100000),
            +(long)((net->n)*sizeof(arc_t) / 0x100000),
            +(long)((net->max_m)*sizeof(arc_t) / 0x100000)
            );
    **/


    net->stop_nodes = net->nodes + net->n + 1; 
    net->stop_arcs  = net->arcs + net->m;
    net->stop_dummy = net->dummy_arcs + net->n;


    node = net->nodes;
    arc = net->arcs;

    for( i = 1; i <= net->n_trips; i++ )
    {
        fgets( instring, 200, in );

        if( sscanf( instring, "%ld %ld", &t, &h ) != 2 || t > h )
            return -1;

        node[i].number = -i;
        node[i].flow = (flow_t)-1;
            
        node[i+net->n_trips].number = i;
        node[i+net->n_trips].flow = (flow_t)1;
        
        node[i].time = t;
        node[i+net->n_trips].time = h;

        arc->tail = &(node[net->n]);
        arc->head = &(node[i]);
        arc->org_cost = arc->cost = (cost_t)(net->bigM+15);
        arc->nextout = arc->tail->firstout;
        arc->tail->firstout = arc;
        arc->nextin = arc->head->firstin;
        arc->head->firstin = arc; 
        arc++;
                                    
        arc->tail = &(node[i+net->n_trips]);
        arc->head = &(node[net->n]);
        arc->org_cost = arc->cost = (cost_t)15;
        arc->nextout = arc->tail->firstout;
        arc->tail->firstout = arc;
        arc->nextin = arc->head->firstin;
        arc->head->firstin = arc; 
        arc++;

        arc->tail = &(node[i]);
        arc->head = &(node[i+net->n_trips]);
        arc->org_cost = arc->cost = (cost_t)(2*MAX(net->bigM,(long)BIGM));
        arc->nextout = arc->tail->firstout;
        arc->tail->firstout = arc;
        arc->nextin = arc->head->firstin;
        arc->head->firstin = arc; 
        arc++;
    }

    
    if( i != net->n_trips + 1 )
        return -1;


    for( i = 0; i < net->m_org; i++, arc++ )
    {
        fgets( instring, 200, in );
        
        if( sscanf( instring, "%ld %ld %ld", &t, &h, &c ) != 3 )
                return -1;

        arc->tail = &(node[t+net->n_trips]);
        arc->head = &(node[h]);
        arc->org_cost = (cost_t)c;
        arc->cost = (cost_t)c;
        arc->nextout = arc->tail->firstout;
        arc->tail->firstout = arc;
        arc->nextin = arc->head->firstin;
        arc->head->firstin = arc; 
    }


    if( net->stop_arcs != arc )
    {
        net->stop_arcs = arc;
        arc = net->arcs;
        for( net->m = 0; arc < net->stop_arcs; arc++ )
            (net->m)++;
        net->m_org = net->m;
    }
    
    fclose( in );


    net->clustfile[0] = (char)0;
        
    for( i = 1; i <= net->n_trips; i++ )
    {
        net->arcs[3*i-1].cost = 
            (cost_t)((-2)*MAX(net->bigM,(long) BIGM));
        net->arcs[3*i-1].org_cost = 
            (cost_t)((-2)*(MAX(net->bigM,(long) BIGM)));
    }
    
    
    return 0;
}
