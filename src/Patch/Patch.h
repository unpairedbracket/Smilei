#ifndef PATCH_H
#define PATCH_H

#include <vector>
#include <iostream>
#include <cstdlib>
#include <iomanip>
#include <limits.h>

#include "Params.h"
#include "SmileiMPI.h"
#include "SimWindow.h"
#include "PartWall.h"
#include "Collisions.h"

class Diagnostic;
class SimWindow;

//! Class Patch :
//!   - data container
//!   - sub MPI domain + MPI methods
//! Collection of patch = MPI domain
class Patch
{
    friend class SmileiMPI;
    friend class VectorPatch;
    friend class SimWindow;
    friend class SyncVectorPatch;
public:
    //! Constructor for Patch
    Patch(Params& params, SmileiMPI* smpi, unsigned int ipatch, unsigned int n_moved);
    //! Cloning Constructor for Patch
    Patch(Patch* patch, Params& params, SmileiMPI* smpi, unsigned int ipatch, unsigned int n_moved);

    //! First initialization step for patches
    void initStep1(Params& params);
    //! Second initialization step for patches
    virtual void initStep2(Params& params) = 0;
    //! Third initialization step for patches
    void initStep3(Params& params, SmileiMPI* smpi, unsigned int n_moved);
    //! Last creation step
    void finishCreation( Params& params, SmileiMPI* smpi );
    //! Last cloning step
    void finishCloning( Patch* patch, Params& params, SmileiMPI* smpi );

    //! Destructor for Patch
    ~Patch();

    // Main PIC objects : data & operators
    // -----------------------------------

    //! Species, Particles, of the current Patch
    std::vector<Species*> vecSpecies;
    //! Electromagnetic fields and densities (E, B, J, rho) of the current Patch
    ElectroMagn* EMfields;

    //! Optional internal boundary condifion on Particles
    PartWalls * partWalls;
    //! Optional binary collisions operators
    std::vector<Collisions*> vecCollisions;

    //! Interpolator (used to push particles and for probes)
    Interpolator* Interp;
    //! Projector
    Projector* Proj;

    std::vector<Diagnostic*> localDiags;

    // Geometrical description
    // -----------------------

    //!Hilbert index of the patch. Number of the patch along the Hilbert curve.
    unsigned int hindex;

    //!Cartesian coordinates of the patch. X,Y,Z of the Patch according to its Hilbert index.
    std::vector<unsigned int> Pcoordinates;


    // MPI exchange/sum methods for particles/fields
    //   - fields communication specified per geometry (pure virtual)
    // --------------------------------------------------------------

    //! manage Idx of particles per direction,
    void initExchParticles(SmileiMPI* smpi, int ispec, Params& params);
    //!init comm  nbr of particles/
    void initCommParticles(SmileiMPI* smpi, int ispec, Params& params, int iDim, VectorPatch* vecPatch);
    //! finalize comm / nbr of particles, init exch / particles
    void CommParticles(SmileiMPI* smpi, int ispec, Params& params, int iDim, VectorPatch* vecPatch);
    //! finalize exch / particles, manage particles suppr/introduce
    void finalizeCommParticles(SmileiMPI* smpi, int ispec, Params& params, int iDim, VectorPatch* vecPatch);
    //! delete Particles included in the index of particles to exchange. Assumes indexes are sorted.
    void cleanup_sent_particles(int ispec, std::vector<int>* indexes_of_particles_to_exchange);

    //! init comm / sum densities
    virtual void initSumField( Field* field, int iDim ) = 0;
    //! finalize comm / sum densities
    virtual void finalizeSumField( Field* field, int iDim ) = 0;

    //! init comm / exchange fields
    virtual void initExchange( Field* field ) = 0;
    //! finalize comm / exchange fields
    virtual void finalizeExchange( Field* field ) = 0;
    //! init comm / exchange fields in direction iDim only
    virtual void initExchange( Field* field, int iDim ) = 0;
    //! finalize comm / exchange fields in direction iDim only
    virtual void finalizeExchange( Field* field, int iDim ) = 0;

    // Create MPI_Datatype to exchange fields
    virtual void createType( Params& params ) = 0;

    // Geometrical methods
    // --------------------

    //! Return the hibert index of current patch
    inline unsigned int Hindex() { return  hindex; }

    //! Method to identify the rank 0 MPI process
    inline bool isMaster() { return (hindex==0); }

    //! Should be pure virtual, see child classes
    inline bool isWestern()  { return locateOnBorders(0, 0); }
    //! Should be pure virtual, see child classes
    inline bool isEastern()  { return locateOnBorders(0, 1); }
    //! Should be pure virtual, see child classes
    inline bool isSouthern() { return locateOnBorders(1, 0); }
    //! Should be pure virtual, see child classes
    inline bool isNorthern() { return locateOnBorders(1, 1); }

    //! Test neighbbor's patch Id to apply or not a boundary condition
    inline bool locateOnBorders(int dir, int way) {
    if ( neighbor_[dir][way] == MPI_PROC_NULL )
        return true;
    return false;
    }

    //! Return MPI rank of this->hrank +/- 1
    //! Should be replaced by an analytic formula
    virtual int getMPIRank(int hrank_pm1) {
        ERROR("Should not happen");
        return 0;
    }

    //! Compute MPI rank of neigbors patch regarding neigbors patch Ids
    void updateMPIenv(SmileiMPI *smpi);

    // Test who is MPI neighbor of current patch
    inline bool is_a_MPI_neighbor(int iDim, int iNeighbor) {
    return( (neighbor_[iDim][iNeighbor]!=MPI_PROC_NULL) && (MPI_neighbor_[iDim][iNeighbor]!=MPI_me_) );
    }

    //! Return real (excluding oversize) min coordinates (ex : rank 0 returns 0.) for direction i
    //! @see min_local
    inline double getDomainLocalMin(int i) const {
        return min_local[i];
    }
    //! Return real (excluding oversize) min coordinates (ex : rank 0 returns 0.) for direction i
    //! @see min_local
    inline double getDomainLocalMax(int i) const {
        return max_local[i];
    }
    //! Return global starting (including oversize, ex : rank 0 returns -oversize) index for direction i
    //! \param i direction
    //! @see cell_starting_global_index
    inline int    getCellStartingGlobalIndex(int i) const {
        return cell_starting_global_index[i];
    }
    //! Set global starting index for direction i
    //! @see cell_starting_global_index
    inline int&    getCellStartingGlobalIndex(int i)  {
        return cell_starting_global_index[i];
    }
    //! Set real min coordinate for direction i
    //! @see min_local
    inline double& getDomainLocalMin(int i)  {
        return min_local[i];
    }
    //! Set real max coordinate for direction i
    //! @see max_local
    inline double& getDomainLocalMax(int i)  {
        return max_local[i];
    }
    //! Return real (excluding oversize) min coordinates (ex : rank 0 retourn 0.) for direction i
    //! @see min_local
    inline std::vector<double> getDomainLocalMin() const {
        return min_local;
    }

    //! Set geometry data in case of moving window restart
    //! \param x_moved difference on coordinates regarding t0 geometry
    //! \param idx_moved number of displacement of the window
    inline void updateMvWinLimits(double x_moved, int idx_moved) {
        min_local[0] += x_moved;
        max_local[0] += x_moved;
        //cell_starting_global_index[0] = (idx_moved-oversize[0]);
        cell_starting_global_index[0] += (idx_moved);
    }

    //! MPI rank of current patch
    int MPI_me_;

protected:
    // Complementary members for the description of the geometry
    // ---------------------------------------------------------

    //! Store number of space dimensions for the fields
    int nDim_fields_;

    //! Number of MPI process per direction in the cartesian topology (2)
    int nbNeighbors_;

    //! Hilbert index of neighbors patch
    std::vector< std::vector<int> > neighbor_;
    //! Hilbert index of corners neighbors patch
    std::vector< std::vector<int> > corner_neighbor_; // Kept for Moving Windows

    //! MPI rank of neighbors patch
    std::vector< std::vector<int> > MPI_neighbor_;

    //! "Real" min limit of local sub-subdomain (ghost data not concerned)
    //!     - "0." on rank 0
    std::vector<double> min_local;
    //! "Real" max limit of local sub-subdomain (ghost data not concerned)
    std::vector<double> max_local;
    //! cell_starting_global_index : index of 1st cell of local sub-subdomain in the global domain.
    //!     - concerns ghost data
    //!     - "- oversize" on rank 0
    std::vector<int> cell_starting_global_index;

    std::vector<unsigned int> oversize;

};


//! Return a unique id to identify all MPI communications
//!  - 2 MPI process can have several communications in the same direction for the same operation
//!  - the communication is identientified using :
//!      - hilbert index of the sender + idir + ineighbor
inline int buildtag(int hindex, int send, int recv) {
    std::stringstream stag("");
    stag << hindex << send  << recv;
    long long int tag(0);
    stag >> tag;
    return (int)(tag);
}


#endif