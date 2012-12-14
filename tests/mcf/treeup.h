/**************************************************************************
FILE: treeup.h

AUTHOR: Andreas Loebel

    Konrad-Zuse-Zentrum fuer Informationstechnik Berlin (ZIB)
    Takustr. 7
    14195 Berlin-Dahlem

Copyright (c) 1998,1999   ZIB Berlin   All Rights Reserved
**************************************************************************/
/*  LAST EDIT: Mon Feb 22 17:42:55 1999 by Andreas Loebel (opt0)  */


#ifndef _TREEUP_H
#define _TREEUP_H


#include "defines.h"


extern void update_tree _PROTO_(( long, long, flow_t, flow_t, node_t *, 
                                  node_t *, node_t *, node_t *, node_t *, 
                                  arc_t *, cost_t, flow_t ));


#endif
