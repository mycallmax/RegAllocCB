/**************************************************************************
FILE: pstart.c

AUTHOR: Andreas Loebel

    Konrad-Zuse-Zentrum fuer Informationstechnik Berlin (ZIB)
    Takustr. 7
    14195 Berlin-Dahlem

Copyright (c) 1998,1999   ZIB Berlin   All Rights Reserved
**************************************************************************/
/*  LAST EDIT: Mon Feb 22 17:42:30 1999 by Andreas Loebel (opt0)  */



#include "pstart.h"




#ifdef _PROTO_ 
long primal_start_artificial( network_t *net )
#else
long primal_start_artificial( net )
    network_t *net;
#endif
{      
    node_t *node, *root;
    arc_t *arc;
    void *stop;


    root = node = net->nodes; node++;
    root->basic_arc = NULL;
    root->pred = NULL;
    root->child = node;
    root->sibling = NULL;
    root->sibling_prev = NULL;
    root->depth = (net->n) + 1;
    root->orientation = 0;
    root->potential = (cost_t) -MAX_ART_COST;
    root->flow = ZERO;

    stop = (void *)net->stop_arcs;
    for( arc = net->arcs; arc != (arc_t *)stop; arc++ )
        if( arc->ident != FIXED )
            arc->ident = AT_LOWER;

    arc = net->dummy_arcs;
    for( stop = (void *)net->stop_nodes; node != (node_t *)stop; arc++, node++ )
    {
        node->basic_arc = arc;
        node->pred = root;
        node->child = NULL;
        node->sibling = node + 1; 
        node->sibling_prev = node - 1;
        node->depth = 1;

        arc->cost = (cost_t) MAX_ART_COST;
        arc->ident = BASIC;

        node->orientation = UP; 
        node->potential = ZERO;
        arc->tail = node;
        arc->head = root;                
        node->flow = (flow_t)0;
    }

    node--; root++;
    node->sibling = NULL;
    root->sibling_prev = NULL;

    return 0;
}
