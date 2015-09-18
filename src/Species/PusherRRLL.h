/*! @file Pusher.h

 @brief Pusher.h  generic class for the particle pusher

 @date 2013-02-15
 */

#ifndef PUSHERRRLL_H
#define PUSHERRRLL_H

#include "Pusher.h"

//  --------------------------------------------------------------------------------------------------------------------
//! Class PusherBoris
//  --------------------------------------------------------------------------------------------------------------------
class PusherRRLL: public Pusher {
public:
    //! Creator for Pusher
    PusherRRLL(Params& params, int ispec);
    ~PusherRRLL();
    //! Overloading of () operator
    virtual void operator() (Particles &particles, int ipart, LocalFields Epart, LocalFields Bpart, double& gf);

};

#endif

