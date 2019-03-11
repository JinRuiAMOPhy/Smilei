// ----------------------------------------------------------------------------
//! \file Merging.cpp
//
//! \brief Class implementation for the generic class
//!  Merging dedicated to the particle merging.
//
//! Creation - 01/2019 - Mathieu Lobet
//
// ----------------------------------------------------------------------------

#include "Merging.h"

// -----------------------------------------------------------------------------
//! Constructor for Merging
// input: simulation parameters & Species index
//! \param params simulation parameters
//! \param species Species index
// -----------------------------------------------------------------------------
Merging::Merging( Params &params, Species *species )
{
    ppc_min_threshold_ = species->merge_ppc_min_threshold_;
}

// -----------------------------------------------------------------------------
//! Destructor for Merging
// -----------------------------------------------------------------------------
Merging::~Merging()
{
}
