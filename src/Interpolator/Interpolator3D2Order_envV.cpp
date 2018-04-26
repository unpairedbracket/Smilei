#include "Interpolator3D2Order_envV.h"

#include <cmath>
#include <iostream>

#include "ElectroMagn.h"
#include "Field3D.h"
#include "Particles.h"

using namespace std;


// ---------------------------------------------------------------------------------------------------------------------
// Creator for Interpolator3D2Order_envV
// ---------------------------------------------------------------------------------------------------------------------
Interpolator3D2Order_envV::Interpolator3D2Order_envV(Params &params, Patch *patch) : Interpolator3D(params, patch)
{

    dx_inv_ = 1.0/params.cell_length[0];
    dy_inv_ = 1.0/params.cell_length[1];
    dz_inv_ = 1.0/params.cell_length[2];
    D_inv[0] = 1.0/params.cell_length[0];
    D_inv[1] = 1.0/params.cell_length[1];
    D_inv[2] = 1.0/params.cell_length[2];

}

// ---------------------------------------------------------------------------------------------------------------------
// 2nd Order_envV Interpolation of the fields at a the particle position (3 nodes are used)
// ---------------------------------------------------------------------------------------------------------------------
void Interpolator3D2Order_envV::operator() (ElectroMagn* EMfields, Particles &particles, int ipart, double* ELoc, double* BLoc)
{
}

void Interpolator3D2Order_envV::operator() (ElectroMagn* EMfields, Particles &particles, SmileiMPI* smpi, int *istart, int *iend, int ithread)
{
    if ( istart[0] == iend[0] ) return; //Don't treat empty cells.

    int nparts( particles.size() );

    double *Epart[3], *Bpart[3];
    double E,E2;

    double *deltaO[3];
    deltaO[0] = &(smpi->dynamics_deltaold[ithread][0]);
    deltaO[1] = &(smpi->dynamics_deltaold[ithread][nparts]);
    deltaO[2] = &(smpi->dynamics_deltaold[ithread][2*nparts]);

    for (unsigned int k=0; k<3;k++) {   
        Epart[k]= &(smpi->dynamics_Epart[ithread][k*nparts]);
        Bpart[k]= &(smpi->dynamics_Bpart[ithread][k*nparts]);
    }

    int idx[3], idxO[3];
    //Primal indices are constant over the all cell
    idx[0]  = round( particles.position(0,*istart) * D_inv[0] );
    idxO[0] = idx[0] - i_domain_begin ;
    idx[1]  = round( particles.position(1,*istart) * D_inv[1] );
    idxO[1] = idx[1] - j_domain_begin ;
    idx[2]  = round( particles.position(2,*istart) * D_inv[2] );
    idxO[2] = idx[2] - k_domain_begin ;

    double ***Egrid[3], ***Bgrid[3];

    Egrid[0] = (static_cast<Field3D*>(EMfields->Ex_))->data_3D;
    Egrid[1] = (static_cast<Field3D*>(EMfields->Ey_))->data_3D;
    Egrid[2] = (static_cast<Field3D*>(EMfields->Ez_))->data_3D;
    Bgrid[0] = (static_cast<Field3D*>(EMfields->Bx_m))->data_3D;
    Bgrid[1] = (static_cast<Field3D*>(EMfields->By_m))->data_3D;
    Bgrid[2] = (static_cast<Field3D*>(EMfields->Bz_m))->data_3D;

    Field3D* Ex3D = static_cast<Field3D*>(EMfields->Ex_);
    Field3D* Ey3D = static_cast<Field3D*>(EMfields->Ey_);
    Field3D* Ez3D = static_cast<Field3D*>(EMfields->Ez_);
    Field3D* Bx3D = static_cast<Field3D*>(EMfields->Bx_m);
    Field3D* By3D = static_cast<Field3D*>(EMfields->By_m);
    Field3D* Bz3D = static_cast<Field3D*>(EMfields->Bz_m);

    double *gradPHI[3], *gradPHIold[3];
    double *PHI, *PHIold;

    Field3D* AA;
    Field3D* AAold;

    if (EMfields->envelope!=NULL) {
        for (unsigned int k=0; k<3;k++) {   
            gradPHI   [k]= &(smpi->dynamics_GradPHIpart   [ithread][k*nparts]);
            gradPHIold[k]= &(smpi->dynamics_GradPHIoldpart[ithread][k*nparts]);
        }
        PHI    = &(smpi->dynamics_PHIpart[ithread][0]);
        PHIold = &(smpi->dynamics_PHIoldpart[ithread][0]);

        //AA    = static_cast<Field3D*>(EMfields->envelope->AA_);
        //AAold = static_cast<Field3D*>(EMfields->envelope->AA_old);

    }

    //double coeff[3][2][3][32]; 
    // Suppress temporary particles dimension of buffers to use the compute function
    double coeff[3][2][3]; 
    int dual[3][32]; // Size ndim. Boolean indicating if the part has a dual indice equal to the primal one (dual=0) or if it is +1 (dual=1).

    int vecSize = 32;

    int cell_nparts( (int)iend[0]-(int)istart[0] );
    int nbVec = ( iend[0]-istart[0]+(cell_nparts-1)-((iend[0]-istart[0]-1)&(cell_nparts-1)) ) / vecSize;

    if (nbVec*vecSize != cell_nparts)
        nbVec++;

    for (int iivect=0 ; iivect<nbVec; iivect++ ){
        int ivect = vecSize*iivect;

        int np_computed(0);
        if (cell_nparts > vecSize ) {
            np_computed = vecSize;
            cell_nparts -= vecSize;
        }       
        else
            np_computed = cell_nparts;

        #pragma omp simd
        for (int ipart=0 ; ipart<np_computed; ipart++ ){

            double delta0, delta;
            double delta2;
            

            for (int i=0;i<3;i++) { // for X/Y
                delta0 = particles.position(i,ipart+ivect+istart[0])*D_inv[i];
                dual [i][ipart] = ( delta0 - (double)idx[i] >=0. );

                for (int j=0;j<2;j++) { // for dual

                    delta   = delta0 - (double)idx[i] + (double)j*(0.5-dual[i][ipart]);
                    delta2  = delta*delta;

                    coeff[i][j][0]     =  0.5 * (delta2-delta+0.25);
                    coeff[i][j][1]     =  (0.75 - delta2);
                    coeff[i][j][2]     =  0.5 * (delta2+delta+0.25);
    
                    if (j==0) deltaO[i][ipart+ivect+istart[0]] = delta;
                }
            }

            int ip = idxO[0] ;
            int id = idxO[0] +dual[0][ipart];
            int jp = idxO[1] ;
            int jd = idxO[1] +dual[1][ipart];
            int kp = idxO[2] ;
            int kd = idxO[2] +dual[2][ipart];

            int cpart = ipart+ivect+istart[0];

            Epart[0][cpart] = compute( &(coeff[0][1][1] ), &(coeff[1][0][1] ), &(coeff[2][0][1] ), Ex3D, id, jp, kp);
            Epart[1][cpart] = compute( &(coeff[0][0][1] ), &(coeff[1][1][1] ), &(coeff[2][0][1] ), Ey3D, ip, jd, kp);
            Epart[2][cpart] = compute( &(coeff[0][0][1] ), &(coeff[1][0][1] ), &(coeff[2][1][1] ), Ez3D, ip, jp, kd);
            Bpart[0][cpart] = compute( &(coeff[0][0][1] ), &(coeff[1][1][1] ), &(coeff[2][1][1] ), Bx3D, ip, jd, kd);
            Bpart[1][cpart] = compute( &(coeff[0][1][1] ), &(coeff[1][0][1] ), &(coeff[2][1][1] ), By3D, id, jp, kd);
            Bpart[2][cpart] = compute( &(coeff[0][1][1] ), &(coeff[1][1][1] ), &(coeff[2][0][1] ), Bz3D, id, jd, kp);

            //PHI   [cpart] = compute( &(coeff[0][0][1] ), &(coeff[1][0][1] ), &(coeff[2][0][1] ), AA   , ip, jp, kp);
            //PHIold[cpart] = compute( &(coeff[0][0][1] ), &(coeff[1][0][1] ), &(coeff[2][0][1] ), AAold, ip, jp, kp);

            //gradPHI[0][cpart] = compute_gradX_and_interpolate( .... AA ....)
            //gradPHI[1][cpart] = compute_gradY_and_interpolate( .... AA ....)
            //gradPHI[2][cpart] = compute_gradZ_and_interpolate( .... AA ....)
            // ...

        } // End of ipart
 
    } // End of iivect

} // END Interpolator3D2Order_envV

void Interpolator3D2Order_envV::operator() (ElectroMagn* EMfields, Particles &particles, int ipart, LocalFields* ELoc, LocalFields* BLoc, LocalFields* JLoc, double* RhoLoc)
{
}