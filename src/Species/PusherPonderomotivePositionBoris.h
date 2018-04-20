/*! @file PusherBoris.h

 @brief PusherBoris.h  generic class for the particle pusher of Boris adapted for envelope model. Only pushes momentum, not their position

 @date 2013-02-15
 */

#ifndef PUSHERPONDEROMOTIVEPOSITIONBORIS_H
#define PUSHERPONDEROMOTIVEPOSITIONBORIS_H

#include "Pusher.h"

//  --------------------------------------------------------------------------------------------------------------------
//! Class PusherPonderomotiveBoris, only pushes momentum of particles interacting with envelope, not their position
//  --------------------------------------------------------------------------------------------------------------------
class PusherPonderomotivePositionBoris : public Pusher {
public:
    //! Creator for Pusher
    PusherPonderomotivePositionBoris(Params& params, Species *species);
    ~PusherPonderomotivePositionBoris();
    //! Overloading of () operator
    virtual void operator() (Particles &particles, SmileiMPI* smpi, int istart, int iend, int ithread);

};

#endif
