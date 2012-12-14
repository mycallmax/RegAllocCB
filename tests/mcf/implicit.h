/**************************************************************************
FILE: implicit.h

AUTHOR: Andreas Loebel

    Konrad-Zuse-Zentrum fuer Informationstechnik Berlin (ZIB)
    Takustr. 7
    14195 Berlin-Dahlem

Copyright (c) 1998,1999   ZIB Berlin   All Rights Reserved
**************************************************************************/
/*  LAST EDIT: Mon Feb 22 17:40:39 1999 by Andreas Loebel (opt0)  */


#ifndef _IMPLICIT_H
#define _IMPLICIT_H


#include "mcfutil.h"
#include "limits.h"


extern long price_out_impl _PROTO_(( network_t * ));
extern long suspend_impl _PROTO_(( network_t *, cost_t, long ));


#endif
