/**************************************************************************
FILE: pbeampp.h

AUTHOR: Andreas Loebel

    Konrad-Zuse-Zentrum fuer Informationstechnik Berlin (ZIB)
    Takustr. 7
    14195 Berlin-Dahlem

Copyright (c) 1998,1999   ZIB Berlin   All Rights Reserved
**************************************************************************/
/*  LAST EDIT: Mon Feb 22 17:41:54 1999 by Andreas Loebel (opt0)  */


#ifndef _PBEAMPP_H
#define _PBEAMPP_H


#include "defines.h"


extern arc_t *primal_bea_mpp _PROTO_(( long, arc_t*, arc_t*, cost_t* ));


#endif
