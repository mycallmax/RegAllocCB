/**************************************************************************
FILE: pbla.h

AUTHOR: Andreas Loebel

    Konrad-Zuse-Zentrum fuer Informationstechnik Berlin (ZIB)
    Takustr. 7
    14195 Berlin-Dahlem

Copyright (c) 1998,1999   ZIB Berlin   All Rights Reserved
**************************************************************************/
/*  LAST EDIT: Mon Feb 22 17:42:03 1999 by Andreas Loebel (opt0)  */


#ifndef _PBLA_H
#define _PBLA_H


#include "defines.h"


extern node_t *primal_iminus _PROTO_(( flow_t *, long *, node_t *, 
                                       node_t *, node_t ** ));


#endif
