/**************************************************************************
FILE: mcfutil.h 

AUTHOR: Andreas Loebel

    Konrad-Zuse-Zentrum fuer Informationstechnik Berlin (ZIB)
    Takustr. 7
    14195 Berlin-Dahlem

Copyright (c) 1998,1999   ZIB Berlin   All Rights Reserved
**************************************************************************/
/*  LAST EDIT: Fri Feb 26 00:25:32 1999 by Andreas Loebel (alf)  */


#ifndef _MCFUTIL_H
#define _MCFUTIL_H


#include "defines.h"


extern void refresh_neighbour_lists _PROTO_(( network_t * ));
extern long refresh_potential _PROTO_(( network_t * ));
extern double flow_cost _PROTO_(( network_t * ));
extern double flow_org_cost _PROTO_(( network_t * ));
extern long primal_feasible _PROTO_(( network_t * ));
extern long dual_feasible _PROTO_(( network_t * ));
extern long getfree _PROTO_(( network_t * ));


#endif
