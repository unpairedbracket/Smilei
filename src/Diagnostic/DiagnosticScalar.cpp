#include "DiagnosticScalar.h"

#include <string>
#include <iomanip>

#include "Params.h"
#include "SmileiMPI.h"
#include "ElectroMagn.h"

using namespace std;

// file open 
void DiagnosticScalar::openFile(SmileiMPI* smpi) {
    if (smpi->isMaster()) {
        fout.open("scalars.txt");
        if (!fout.is_open()) ERROR("Can't open scalar file");
    }
}

void DiagnosticScalar::closeFile(SmileiMPI* smpi) {
    if (smpi->isMaster()) {
        fout.close();
    }
}


// ---------------------------------------------------------------------------------------------------------------------
// Wrapers of the methods
// ---------------------------------------------------------------------------------------------------------------------
void DiagnosticScalar::run(int timestep, ElectroMagn* EMfields, vector<Species*>& vecSpecies, SmileiMPI *smpi) {
    
    // at timestep=0 initialize the energies
    if (timestep==0) {
        compute(EMfields,vecSpecies,smpi);
        Energy_time_zero  = getScalar("Utot");
        EnergyUsedForNorm = Energy_time_zero;
    }
    
    double time = (double)timestep * dt;
    // check that every is defined for scalar & that tmin <= time <= tmax
    if ( (every) && (time >= tmin) && (time <= tmax) ) {

        EMfields->computePoynting(); // Poynting must be calculated & incremented at every timesteps
        
        if (timestep % every == 0) { // other scalars are calculated only at timestep \propto every
            compute(EMfields,vecSpecies,smpi);
            if (smpi->isMaster()) {
                write(timestep);
            }
        }
        
    }
    
}


// ---------------------------------------------------------------------------------------------------------------------
// Contains all to manage the communication of data. It is "transparent" to the user.
// ---------------------------------------------------------------------------------------------------------------------
void DiagnosticScalar::compute (ElectroMagn* EMfields, vector<Species*>& vecSpecies, SmileiMPI *smpi) {
    out_list.clear();
    
    // ------------------------
    // SPECIES-related energies
    // ------------------------
    double Ukin=0.;             // total (kinetic) energy carried by particles (test-particles do not contribute)
    double Ukin_bnd=0.;         // total energy lost by particles due to boundary conditions
    double Ukin_out_mvw=0.;     // total energy lost due to particles being suppressed by the moving-window
    double Ukin_inj_mvw=0.;     // total energy added due to particles created by the moving-window
    
    // Compute scalars for each species
    for (unsigned int ispec=0; ispec<vecSpecies.size(); ispec++) {
        if (vecSpecies[ispec]->particles.isTestParticles) continue;    // No scalar diagnostic for test particles
        
        double charge_avg=0.0;  // average charge of current species ispec
        double ener_tot=0.0;    // total kinetic energy of current species ispec

        unsigned int nPart=vecSpecies[ispec]->getNbrOfParticles(); // number of particles
        if (nPart>0) {
            for (unsigned int iPart=0 ; iPart<nPart; iPart++ ) {
                charge_avg += (double)vecSpecies[ispec]->particles.charge(iPart);
                ener_tot   += cell_volume * vecSpecies[ispec]->particles.weight(iPart)
                *             (vecSpecies[ispec]->particles.lor_fac(iPart)-1.0);
            }
            ener_tot*=vecSpecies[ispec]->species_param.mass;
        }//if
        
        MPI_Reduce(smpi->isMaster()?MPI_IN_PLACE:&charge_avg, &charge_avg, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(smpi->isMaster()?MPI_IN_PLACE:&ener_tot, &ener_tot, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(smpi->isMaster()?MPI_IN_PLACE:&nPart, &nPart, 1, MPI_UNSIGNED, MPI_SUM, 0, MPI_COMM_WORLD);
        
        // particle energy lost due to boundary conditions
        double ener_lost_bcs=0.0;
        ener_lost_bcs = vecSpecies[ispec]->getLostNrjBC();
        MPI_Reduce(smpi->isMaster()?MPI_IN_PLACE:&ener_lost_bcs, &ener_lost_bcs, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        
        // particle energy lost due to moving window
        double ener_lost_mvw=0.0;
        ener_lost_mvw = vecSpecies[ispec]->getLostNrjMW();
        MPI_Reduce(smpi->isMaster()?MPI_IN_PLACE:&ener_lost_mvw, &ener_lost_mvw, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        
        // particle energy added due to moving window
        double ener_added_mvw=0.0;
        ener_added_mvw = vecSpecies[ispec]->getNewParticlesNRJ();
        MPI_Reduce(smpi->isMaster()?MPI_IN_PLACE:&ener_added_mvw, &ener_added_mvw, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        
        // appending scalar output
        if (smpi->isMaster()) {
            if (nPart!=0) charge_avg /= nPart;
            string nameSpec=vecSpecies[ispec]->species_param.species_type;
            append("Ntot_"+nameSpec,nPart);
            append("Zavg_"+nameSpec,charge_avg);
            append("Ukin_"+nameSpec,ener_tot);
            
            // incremement the total kinetic energy
            Ukin += ener_tot;
            
            // increment all energy loss & energy input
            Ukin_bnd        += cell_volume*ener_lost_bcs;
            Ukin_out_mvw    += cell_volume*ener_lost_mvw;
            Ukin_inj_mvw    += cell_volume*ener_added_mvw;
        }
        
        vecSpecies[ispec]->reinitDiags();
        
    }//for ispec
    
    
    // --------------------------------
    // ELECTROMAGNETIC-related energies
    // --------------------------------
    
    vector<Field*> fields;
    
    fields.push_back(EMfields->Ex_);
    fields.push_back(EMfields->Ey_);
    fields.push_back(EMfields->Ez_);
    fields.push_back(EMfields->Bx_m);
    fields.push_back(EMfields->By_m);
    fields.push_back(EMfields->Bz_m);
    
    // Compute all electromagnetic energies
    // ------------------------------------
    
    double Uelm=0.0; // total electromagnetic energy in the fields
    
    // loop on all electromagnetic fields
    for (vector<Field*>::iterator field=fields.begin(); field!=fields.end(); field++) {
        
        map<string,val_index> scalars_map;
        
        double Utot_crtField=0.0; // total energy in current field
        
        // compute the starting/ending points of each fields (w/out ghost cells) as well as the field global size
        vector<unsigned int> iFieldStart(3,0), iFieldEnd(3,1), iFieldGlobalSize(3,1);
        for (unsigned int i=0 ; i<(*field)->isDual_.size() ; i++ ) {
            iFieldStart[i]      = EMfields->istart[i][(*field)->isDual(i)];
            iFieldEnd[i]        = iFieldStart[i] + EMfields->bufsize[i][(*field)->isDual(i)];
            iFieldGlobalSize[i] = (*field)->dims_[i];
        }
        
        // loop on all (none-ghost) cells & add-up the squared-field to the energy density
        for (unsigned int k=iFieldStart[2]; k<iFieldEnd[2]; k++) {
            for (unsigned int j=iFieldStart[1]; j<iFieldEnd[1]; j++) {
                for (unsigned int i=iFieldStart[0]; i<iFieldEnd[0]; i++) {
                    unsigned int ii=k+ j*iFieldGlobalSize[2] +i*iFieldGlobalSize[1]*iFieldGlobalSize[2];
                    Utot_crtField+=pow((**field)(ii),2);
                }
            }
        }        
        // Utot = Dx^N/2 * Field^2
        Utot_crtField *= 0.5*cell_volume;
        MPI_Reduce(smpi->isMaster()?MPI_IN_PLACE:&Utot_crtField, &Utot_crtField, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        
        if (smpi->isMaster()) {
            append("Uelm_"+(*field)->name,Utot_crtField);
            Uelm+=Utot_crtField;
        }
    }
    
    // nrj lost with moving window (fields)
    double Uelm_out_mvw = EMfields->getLostNrjMW();
    MPI_Reduce(smpi->isMaster()?MPI_IN_PLACE:&Uelm_out_mvw, &Uelm_out_mvw, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
    if (smpi->isMaster()) {
        Uelm_out_mvw *= 0.5*cell_volume;
    }
    
    // nrj added due to moving window (fields)
    double Uelm_inj_mvw=EMfields->getNewFieldsNRJ();
    MPI_Reduce(smpi->isMaster()?MPI_IN_PLACE:&Uelm_inj_mvw, &Uelm_inj_mvw, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
    if (smpi->isMaster()) {
        Uelm_inj_mvw *= 0.5*cell_volume;
    }
    EMfields->reinitDiags();
    
    
    // ---------------------------------------------------------------------------------------
    // ALL FIELDS-RELATED SCALARS: Compute all min/max-related scalars (defined on all fields)
    // ---------------------------------------------------------------------------------------

    // add currents and density to fields
    fields.push_back(EMfields->Jx_);
    fields.push_back(EMfields->Jy_);
    fields.push_back(EMfields->Jz_);
    fields.push_back(EMfields->rho_);
    
    vector<val_index> minis, maxis;
    
    for (vector<Field*>::iterator field=fields.begin(); field!=fields.end(); field++) {
        
        val_index minVal, maxVal;
        
        minVal.val=maxVal.val=(**field)(0);
        minVal.index=maxVal.index=0;
        
        vector<unsigned int> iFieldStart(3,0), iFieldEnd(3,1), iFieldGlobalSize(3,1);
        for (unsigned int i=0 ; i<(*field)->isDual_.size() ; i++ ) {
            iFieldStart[i] = EMfields->istart[i][(*field)->isDual(i)];
            iFieldEnd [i] = iFieldStart[i] + EMfields->bufsize[i][(*field)->isDual(i)];
            iFieldGlobalSize [i] = (*field)->dims_[i];
        }
        for (unsigned int k=iFieldStart[2]; k<iFieldEnd[2]; k++) {
            for (unsigned int j=iFieldStart[1]; j<iFieldEnd[1]; j++) {
                for (unsigned int i=iFieldStart[0]; i<iFieldEnd[0]; i++) {
                    unsigned int ii=k+ j*iFieldGlobalSize[2] +i*iFieldGlobalSize[1]*iFieldGlobalSize[2];
                    if (minVal.val>(**field)(ii)) {
                        minVal.val=(**field)(ii);
                        minVal.index=ii; // rank encoded
                    }
                    if (maxVal.val<(**field)(ii)) {
                        maxVal.val=(**field)(ii);
                        maxVal.index=ii; // rank encoded
                    }
                }
            }
        }
        minis.push_back(minVal);
        maxis.push_back(maxVal);
    }
    
    MPI_Reduce(smpi->isMaster()?MPI_IN_PLACE:&minis[0], &minis[0], minis.size(), MPI_DOUBLE_INT, MPI_MINLOC, 0, MPI_COMM_WORLD);
    MPI_Reduce(smpi->isMaster()?MPI_IN_PLACE:&maxis[0], &maxis[0], maxis.size(), MPI_DOUBLE_INT, MPI_MAXLOC, 0, MPI_COMM_WORLD);
    
    if (smpi->isMaster()) {
        if (minis.size() == maxis.size() && minis.size() == fields.size()) {
            unsigned int i=0;
            for (vector<Field*>::iterator field=fields.begin(); field!=fields.end() && i<minis.size(); field++, i++) {
                
                append((*field)->name+"Min",minis[i].val);
                append((*field)->name+"MinCell",minis[i].index);
                
                append((*field)->name+"Max",maxis[i].val);
                append((*field)->name+"MaxCell",maxis[i].index);
                
            }
        }
        
    }
    
    // ------------------------
    // POYNTING-related scalars
    // ------------------------
    
    // electromagnetic energy injected in the simulation (calculated from Poynting fluxes)
    double Uelm_bnd=0.0;
    
    for (unsigned int j=0; j<2;j++) {
        for (unsigned int i=0; i<EMfields->poynting[j].size();i++) {
            
            double poy[2]={EMfields->poynting[j][i],EMfields->poynting_inst[j][i]};
            MPI_Reduce(smpi->isMaster()?MPI_IN_PLACE:poy, poy, 2, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
            
            if (smpi->isMaster()) {
                string name("Poy");
                switch (i) { // dimension
                    case 0:
                        name+=(j==0?"East":"West");
                        break;
                    case 1:
                        name+=(j==0?"South":"North");
                        break;
                    case 2:
                        name+=(j==0?"Bottom":"Top");
                        break;
                    default:
                        break;
                }
                append(name,poy[0]);
                append(name+"Inst",poy[1]);
                
                Uelm_bnd += poy[0];
                
            }//if isMaster
            
        }// i
    }// j
    
    
    // -----------
    // FINAL steps
    // -----------
    
    if (smpi->isMaster()) {
        
        // total energy in the simulation
        double Utot = Ukin + Uelm;

        // expected total energy
        double Uexp = Energy_time_zero + Uelm_bnd + Ukin_inj_mvw + Uelm_inj_mvw
        -           ( Ukin_bnd + Ukin_out_mvw + Uelm_out_mvw );
        
        // energy balance
        double Ubal = Utot - Uexp;
        
        // energy used for normalization
        EnergyUsedForNorm = Utot;

        // normalized energy balance
        double Ubal_norm(0.);
        if (EnergyUsedForNorm>0.)
            Ubal_norm = Ubal / EnergyUsedForNorm;
        
        // outputs
        // -------
        
        // added & lost energies due to the moving window
        prepend("Ukin_out_mvw",Ukin_out_mvw);
        prepend("Ukin_inj_mvw",Ukin_inj_mvw);
        prepend("Uelm_out_mvw",Uelm_out_mvw);
        prepend("Uelm_inj_mvw",Uelm_inj_mvw);
        
        // added & lost energies at the boundaries
        prepend("Ukin_bnd",Ukin_bnd);
        prepend("Uelm_bnd",Uelm_bnd);
        
        // total energies & energy balance
        prepend("Ukin",Ukin);
        prepend("Uelm",Uelm);
        prepend("Ubal_norm",Ubal_norm);
        prepend("Ubal",Ubal);
        prepend("Uexp",Uexp);
        prepend("Utot",Utot);
        
    }
    
}


bool DiagnosticScalar::allowedKey(string my_var) {
    int s=vars.size();
    if (s==0) return true;
    for( int i=0; i<s; i++) {
        if( my_var==vars[i] ) return true;
    }
    return false;
}


void DiagnosticScalar::prepend(std::string my_var, double val) {
    out_list.insert(out_list.begin(),make_pair(my_var,val));
    
}


void DiagnosticScalar::append(std::string my_var, double val) {
    out_list.push_back(make_pair(my_var,val));
}


void DiagnosticScalar::write(int itime) {
    unsigned int added_length = 15; //! \todo (MG) might be better to compute it to make sure it works for all species name
    fout << std::scientific;
    fout.precision(precision);
    if (fout.tellp()==ifstream::pos_type(0)) {
        fout << "# " << 1 << " time" << endl;
        unsigned int i=2;
        for(vector<pair<string,double> >::iterator iter = out_list.begin(); iter !=out_list.end(); iter++) {
            if (allowedKey((*iter).first) == true) {
                fout << "# " << i << " " << (*iter).first << endl;
                i++;
            }
        }
        
        fout << "#\n#" << setw(precision+added_length) << "time";
        for(vector<pair<string,double> >::iterator iter = out_list.begin(); iter !=out_list.end(); iter++) {
            if (allowedKey((*iter).first) == true) {
                fout << setw(precision+added_length) << (*iter).first;
            }
        }
        fout << setw(precision+added_length) << itime/res_time;
        for(vector<pair<string,double> >::iterator iter = out_list.begin(); iter !=out_list.end(); iter++) {
            if (allowedKey((*iter).first) == true) {
                fout << setw(precision+added_length) << (*iter).second;
            }
        }
        fout << endl;
    }
    fout << setw(precision+9) << itime/res_time;
    for(vector<pair<string,double> >::iterator iter = out_list.begin(); iter !=out_list.end(); iter++) {
        if (allowedKey((*iter).first) == true) {
            fout << setw(precision+9) << (*iter).second;
        }
    }
    fout << endl;
}

// ---------------------------------------------------------------------------------------------------------------------
// Method used to return the (double) scalar defined as "my_var"
// ---------------------------------------------------------------------------------------------------------------------
double DiagnosticScalar::getScalar(string my_var){
    for (unsigned int i=0; i< out_list.size(); i++) {
        if (out_list[i].first==my_var) {
            return out_list[i].second;
        }
    }
    DEBUG("key not found " << my_var);
    return 0.0;
}

