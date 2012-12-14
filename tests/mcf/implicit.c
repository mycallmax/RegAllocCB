/**************************************************************************
File: implicit.c

AUTHOR: Andreas Loebel

    Konrad-Zuse-Zentrum fuer Informationstechnik Berlin (ZIB)
    Takustr. 7
    14195 Berlin-Dahlem

Copyright (c) 1998,1999   ZIB Berlin   All Rights Reserved
**************************************************************************/
/*  LAST EDIT: Thu Mar 18 10:15:37 1999 by Andreas Loebel (opt0)  */


#include "implicit.h"







#ifdef _PROTO_
long resize_prob( network_t *net )
#else
long resize_prob( net )
     network_t *net;
#endif
{
    arc_t *arc;
    node_t *node, *stop, *root;
#ifdef SPEC_CPU2000_P64
    __int64 off;
#else
    long off;
#endif            
            
    net->max_m += MAX_NEW_ARCS;
    
    /**
    printf( "\nresize arcs to %3d MB (%d elements a %d B)\n\n",
            net->max_m * sizeof(arc_t) / 0x100000,
            net->max_m,
            sizeof(arc_t) );
    fflush( stdout );
    **/

    arc = (arc_t *) realloc( net->arcs, net->max_m * sizeof(arc_t) );
    if( !arc )
    {
        printf( "network %s: not enough memory\n", net->inputfile );
        fflush( stdout );
        return -1;
    }
    
#ifdef SPEC_CPU2000_P64
    off = (__int64)arc - (__int64)net->arcs;
#else    
    off = (long)arc - (long)net->arcs;
#endif        
        
    net->arcs = arc;
    net->stop_arcs = arc + net->m;

    root = node = net->nodes;
    for( node++, stop = (void *)net->stop_nodes; node < stop; node++ )
        if( node->pred != root )
#ifdef SPEC_CPU2000_P64
            node->basic_arc = (arc_t *)((__int64)node->basic_arc + off);
#else
            node->basic_arc = (arc_t *)((long)node->basic_arc + off);
#endif                
    return 0;
}







#ifdef _PROTO_
cost_t compute_red_cost( cost_t cost, node_t *tail, cost_t head_potential )
#else
cost_t compute_red_cost( cost, tail, head_potential )
     cost_t cost;
     node_t *tail;
     cost_t head_potential;
#endif
{
    return (cost - tail->potential + head_potential);
}







#ifdef _PROTO_
void insert_new_arc( arc_t *new, long newpos, node_t *tail, node_t *head,
                     cost_t cost, cost_t red_cost )
#else
void insert_new_arc( new, newpos, tail, head, cost, red_cost )
     arc_t *new;
     long newpos;
     node_t *tail;
     node_t *head;
     cost_t cost;
     cost_t red_cost;
#endif
{
    long pos;

    new[newpos].tail      = tail;
    new[newpos].head      = head;
    new[newpos].org_cost  = cost;
    new[newpos].cost      = cost;
    new[newpos].flow      = (flow_t)red_cost; 
    
    pos = newpos+1;
    while( pos-1 && red_cost > (cost_t)new[pos/2-1].flow )
    {
        new[pos-1].tail     = new[pos/2-1].tail;
        new[pos-1].head     = new[pos/2-1].head;
        new[pos-1].cost     = new[pos/2-1].cost;
        new[pos-1].org_cost = new[pos/2-1].cost;
        new[pos-1].flow     = new[pos/2-1].flow;
        
        pos = pos/2;
        new[pos-1].tail     = tail;
        new[pos-1].head     = head;
        new[pos-1].cost     = cost;
        new[pos-1].org_cost = cost;
        new[pos-1].flow     = (flow_t)red_cost; 
    }
    
    return;
}   






#ifdef _PROTO_
void replace_weaker_arc( arc_t *new, node_t *tail, node_t *head,
                         cost_t cost, cost_t red_cost )
#else
void replace_weaker_arc( new, tail, head, cost, red_cost )
     arc_t *new;
     node_t *tail;
     node_t *head;
     cost_t cost;
     cost_t red_cost;
#endif
{
    long pos;
    long cmp;

    new[0].tail     = tail;
    new[0].head     = head;
    new[0].org_cost = cost;
    new[0].cost     = cost;
    new[0].flow     = (flow_t)red_cost; 
                    
    pos = 1;
    cmp = (new[1].flow > new[2].flow) ? 2 : 3;
    while( cmp <= MAX_NEW_ARCS && red_cost < new[cmp-1].flow )
    {
        new[pos-1].tail = new[cmp-1].tail;
        new[pos-1].head = new[cmp-1].head;
        new[pos-1].cost = new[cmp-1].cost;
        new[pos-1].org_cost = new[cmp-1].cost;
        new[pos-1].flow = new[cmp-1].flow;
        
        new[cmp-1].tail = tail;
        new[cmp-1].head = head;
        new[cmp-1].cost = cost;
        new[cmp-1].org_cost = cost;
        new[cmp-1].flow = (flow_t)red_cost; 
        pos = cmp;
        cmp *= 2;
        if( cmp + 1 <= MAX_NEW_ARCS )
            if( new[cmp-1].flow < new[cmp].flow )
                cmp++;
    }
    
    return;
}   






#ifdef _PROTO_
long price_out_impl( network_t *net )
#else
long price_out_impl( net )
     network_t *net;
#endif
{
    long i;
    long trips;
    long new_arcs = 0;
    long resized = 0;
    long latest;
    long min_impl_duration = 15;

    register cost_t bigM = net->bigM;
    register cost_t head_potential;
    register cost_t arc_cost = 30;
    register cost_t red_cost;
    register cost_t bigM_minus_min_impl_duration;
        
    register arc_t *arcout, *arcin, *arcnew, *stop;
    register arc_t *first_of_sparse_list;
    register node_t *tail, *head;
    
    bigM_minus_min_impl_duration = (cost_t)bigM - min_impl_duration;
    
    if( net->m + MAX_NEW_ARCS > net->max_m 
       && (net->n_trips*net->n_trips)/2 + net->m > net->max_m )
    {
        resized = 1;
        if( resize_prob( net ) )
            return -1;

        refresh_neighbour_lists( net );
    }
        
    arcnew = net->stop_arcs;
    trips = net->n_trips;

    arcout = net->arcs;
    for( i = 0; i < trips && arcout[1].ident == FIXED; i++, arcout += 3 );
    first_of_sparse_list = (arc_t *)NULL;
    for( ; i < trips; i++, arcout += 3 )
    {
        if( arcout[1].ident != FIXED )
        {
	        arcout->head->firstout->head->mark = (size_t)first_of_sparse_list;
            first_of_sparse_list = arcout + 1;
        }
        
        if( arcout->ident == FIXED )
            continue;
        
        head = arcout->head;
        latest = head->time - arcout->org_cost 
            + (long)bigM_minus_min_impl_duration;
                
        head_potential = head->potential;
        
        arcin = (arc_t *)first_of_sparse_list->tail->mark;
        while( arcin )
        {
            tail = arcin->tail;

            if( tail->time + arcin->org_cost > latest )
            {
                arcin = (arc_t *)tail->mark;
                continue;
            }
            
            red_cost = compute_red_cost( arc_cost, tail, head_potential );
            
            if( red_cost < 0 )
            {
                if( new_arcs < MAX_NEW_ARCS )
                {
                    insert_new_arc( arcnew, new_arcs, tail, head, 
                                    arc_cost, red_cost );
                    new_arcs++;                 
                }
                else if( (cost_t)arcnew[0].flow > red_cost )
                    replace_weaker_arc( arcnew, tail, head, 
                                        arc_cost, red_cost );
            }

            arcin = (arc_t *)tail->mark;
        }
    }
    
    if( new_arcs )
    {
        arcnew = net->stop_arcs;
        net->stop_arcs += new_arcs;
        stop = (void *)net->stop_arcs;
        if( resized )
        {
            for( ; arcnew != stop; arcnew++ )
            {
                arcnew->flow = (flow_t)0;
                arcnew->ident = AT_LOWER;
            }
        }
        else
        {
            for( ; arcnew != stop; arcnew++ )
            {
                arcnew->flow = (flow_t)0;
                arcnew->ident = AT_LOWER;
                arcnew->nextout = arcnew->tail->firstout;
                arcnew->tail->firstout = arcnew;
                arcnew->nextin = arcnew->head->firstin;
                arcnew->head->firstin = arcnew;
            }
        }
        
        net->m += new_arcs;
        net->m_impl += new_arcs;
    }
    

    return new_arcs;
}   






#ifdef _PROTO_
long suspend_impl( network_t *net, cost_t threshold, long all )
#else
long suspend_impl( net, threshold, all )
     network_t *net;
     cost_t threshold;
     long all;
#endif
{
    long susp;
    
    cost_t red_cost;
    arc_t *new_arc, *arc;
    void *stop;

    

    if( all )
        susp = net->m_impl;
    else
    {
        stop = (void *)net->stop_arcs;
        new_arc = &(net->arcs[net->m - net->m_impl]);
        for( susp = 0, arc = new_arc; arc < (arc_t *)stop; arc++ )
        {
            if( arc->ident == AT_LOWER )
                red_cost = arc->cost - arc->tail->potential 
                        + arc->head->potential;
            else
            {
                red_cost = (cost_t)-2;
                
                if( arc->ident == BASIC )
                {
                    if( arc->tail->basic_arc == arc )
                        arc->tail->basic_arc = new_arc;
                    else
                        arc->head->basic_arc = new_arc;
                }
            }
            
            if( red_cost > threshold )
                susp++;
            else
            {
                *new_arc = *arc;
                new_arc++;
            }
        }
    }
    
        
    if( susp )
    {
        net->m -= susp;
        net->m_impl -= susp;
        net->stop_arcs -= susp;
        
        refresh_neighbour_lists( net );
    }

    return susp;
}



