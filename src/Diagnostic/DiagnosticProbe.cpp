#include "DiagnosticProbe.h"

#include <iomanip>
#include <string>
#include <iomanip>
#include <sstream>

#include "PicParams.h"
#include "SmileiMPI.h"
#include "SmileiMPI_Cart1D.h"
#include "SmileiMPI_Cart2D.h"
#include "ElectroMagn.h"
#include "Field1D.h"
#include "Field2D.h"
#include "Field.h"
#include "DiagParams.h"

using namespace std;

DiagnosticProbe::DiagnosticProbe(PicParams &params, DiagParams &diagParams, SmileiMPI* smpi):
cpuRank((int)smpi->getRank()),
probeSize(10), 
fileId(0) {
    
    if (diagParams.probeStruc.size() >0) {
        hid_t pid = H5Pcreate(H5P_FILE_ACCESS);
        H5Pset_fapl_mpio(pid, MPI_COMM_WORLD, MPI_INFO_NULL);
        fileId = H5Fcreate( "Probes.h5", H5F_ACC_TRUNC, H5P_DEFAULT, pid);
        H5Pclose(pid);
        
        string ver(__VERSION);
        hid_t sid = H5Screate(H5S_SCALAR);
        hid_t tid = H5Tcopy(H5T_C_S1);
        H5Tset_size(tid, ver.size());
        H5Tset_strpad(tid,H5T_STR_NULLTERM);
        hid_t aid = H5Acreate(fileId, "Version", tid, sid, H5P_DEFAULT, H5P_DEFAULT);
        
        H5Awrite(aid, tid, ver.c_str());
        
        H5Aclose(aid);
        H5Sclose(sid);
        H5Tclose(tid);
        
        every.resize(diagParams.probeStruc.size());
        probeParticles.resize(diagParams.probeStruc.size());
        probeId.resize(diagParams.probeStruc.size());
	probesArray.resize(diagParams.probeStruc.size());
	probesStart.resize(diagParams.probeStruc.size());

        for (unsigned int np=0; np<diagParams.probeStruc.size(); np++) {
            every[np]=diagParams.probeStruc[np].every;
            unsigned int dimProbe=diagParams.probeStruc[np].dim+2;
            unsigned int ndim=params.nDim_particle;
            
            vector<unsigned int> vecNumber=diagParams.probeStruc[np].number;
            
            unsigned int totPart=1;
            for (unsigned int iDimProbe=0; iDimProbe<diagParams.probeStruc[np].dim; iDimProbe++) {
                totPart *= vecNumber[iDimProbe];
            }
            
            probeParticles[np].initialize(totPart, ndim);
            probeId[np].resize(totPart);
            
            vector<double> partPos(ndim*totPart,0.0);
            
            for(unsigned int ipart=0; ipart!=totPart; ++ipart) {
                int found=cpuRank;
                for(unsigned int iDim=0; iDim!=ndim; ++iDim) {
                    partPos[iDim+ipart*ndim]=diagParams.probeStruc[np].pos[0][iDim];
                    // the particle position is a linear combiantion of the point pos with posFirst or posSecond or posThird
                    for (unsigned int iDimProbe=0; iDimProbe<diagParams.probeStruc[np].dim; iDimProbe++) {
                        partPos[iDim+ipart*ndim] += (ipart%vecNumber[iDimProbe])*(diagParams.probeStruc[np].pos[iDimProbe+1][iDim]-diagParams.probeStruc[np].pos[0][iDim])/(vecNumber[iDimProbe]-1);
                    }
                    probeParticles[np].position(iDim,ipart) = partPos[iDim+ipart*ndim];
                    
                    //!fixme this is awful: we add one cell if we're on the upper border
                    double maxToCheck=smpi->getDomainLocalMax(iDim);                    
                    if (ndim==1) {
                        if ((static_cast<SmileiMPI_Cart1D*>(smpi))->isEastern()) {
                            maxToCheck+=params.cell_length[iDim];
                        }
                    } else if (ndim==2) {
                        if ((iDim == 0 && (static_cast<SmileiMPI_Cart2D*>(smpi))->isEastern()) ||
                            (iDim == 1 && (static_cast<SmileiMPI_Cart2D*>(smpi))->isNorthern())) {
                            maxToCheck+=params.cell_length[iDim];
                        }                        
                    } else {
                        ERROR("implement here");
                    }

                    if (probeParticles[np].position(iDim,ipart) < smpi->getDomainLocalMin(iDim) ||
                        probeParticles[np].position(iDim,ipart) >= maxToCheck) {
                        found=-1;
                    }
                }
                probeId[np][ipart] = found;
            }

	    nProbeTot = probeParticles[np].size();
	    for ( int ipb=nProbeTot-1 ; ipb>=0 ; ipb--) {
		if (!probeParticles[np].is_part_in_domain(ipb, smpi))
		    probeParticles[np].erase_particle(ipb);
	    }
	    // probesArray : np vectors x 10 vectors x probeParticles[np].size() double
	    vector<unsigned int> probesArraySize(2);
	    probesArraySize[0] = probeParticles[np].size();
	    probesArraySize[1] = probeSize;
	    probesArray[np] = new Field2D(probesArraySize);

	    // probesStart
	    probesStart[np] = 0;
	    MPI_Status status;
	    if (cpuRank>0)
		MPI_Recv( &(probesStart[np]), 1, MPI_INTEGER, cpuRank-1, 0, MPI_COMM_WORLD, &status );
	    
	    int probeEnd = probesStart[np]+probeParticles[np].size();
	    if (cpuRank!=smpi->getSize()-1)
		MPI_Send( &probeEnd, 1, MPI_INTEGER, cpuRank+1, 0, MPI_COMM_WORLD );



            vector<hsize_t> dims(dimProbe);
            vector<hsize_t> max_dims(dimProbe);
            vector<hsize_t> chunk_dims(dimProbe);
            
            dims[0]=0;
            max_dims[0]=H5S_UNLIMITED;
            chunk_dims[0]=1;
            
            for (unsigned int iDimProbe=1; iDimProbe<dimProbe-1; iDimProbe++) {
                dims[iDimProbe]=vecNumber[iDimProbe-1];
                max_dims[iDimProbe]=vecNumber[iDimProbe-1];
                chunk_dims[iDimProbe]=1;
            }
            dims.back()=probeSize;
            max_dims.back()=probeSize;
            chunk_dims.back()=probeSize;
            
            sid = H5Screate_simple(dimProbe, &dims[0], &max_dims[0]);
            hid_t pid = H5Pcreate(H5P_DATASET_CREATE);
            H5Pset_layout(pid, H5D_CHUNKED);
            H5Pset_chunk(pid, dimProbe, &chunk_dims[0]);
            
	    hid_t did = H5Gcreate(fileId, probeName(np).c_str(), H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
            H5Pclose(pid);
            H5Sclose(sid);

	    // vecNumberProd ???
            unsigned int vecNumberProd=1;
            for (unsigned int iDimProbe=0; iDimProbe<vecNumber.size(); iDimProbe++) {
                vecNumberProd*=vecNumber[iDimProbe];
            }  
            
            vector<hsize_t> dimsPos(1+vecNumber.size());
            for (unsigned int iDimProbe=0; iDimProbe<vecNumber.size(); iDimProbe++) {
                dimsPos[iDimProbe]=vecNumber[iDimProbe];
            }
            dimsPos[vecNumber.size()]=ndim;

	    vector<unsigned int> posArraySize(2);
	    posArraySize[0] = probeParticles[np].size();
	    posArraySize[1] = ndim;
	    Field2D* posArray = new Field2D(posArraySize);
	    for ( int ipb=0 ; ipb<probeParticles[np].size() ; ipb++) {
		for (int idim=0 ; idim<ndim  ; idim++ )
		    posArray->data_2D[ipb][idim] = probeParticles[np].position(idim,ipb);
	    }
	    // memspace OK : 1 block 
            hsize_t     chunk_parts[2];
            chunk_parts[0] = probeParticles[np].size();
            chunk_parts[1] = 2; 
	    hid_t memspace  = H5Screate_simple(2, chunk_parts, NULL);
	    // filespace :
	    hsize_t dimsf[2], offset[2], stride[2], count[2];
	    dimsf[0] = nProbeTot;
	    dimsf[1] = 2;
            hid_t filespace = H5Screate_simple(2, dimsf, NULL);
            offset[0] = probesStart[np];
            offset[1] = 0;
            stride[0] = 1;
            stride[1] = 1;
            count[0] = 1;
            count[1] = 1;
            hsize_t     block[2];
            block[0] = probeParticles[np].size();
            block[1] = ndim;
            H5Sselect_hyperslab(filespace, H5S_SELECT_SET, offset, stride, count, block);

	    //define , write_plist
	    hid_t write_plist = H5Pcreate(H5P_DATASET_XFER);
	    H5Pset_dxpl_mpio(write_plist, H5FD_MPIO_INDEPENDENT);
            hid_t plist_id = H5Pcreate(H5P_DATASET_CREATE);
	    hid_t dset_id  = H5Dcreate(did, "positions", H5T_NATIVE_DOUBLE, filespace, H5P_DEFAULT, plist_id, H5P_DEFAULT);
	    H5Pclose(plist_id);
	    H5Dwrite( dset_id, H5T_NATIVE_DOUBLE, memspace, filespace, write_plist, &(posArray->data_2D[0][0]) );
	    H5Dclose(dset_id);
	    H5Pclose( write_plist );

	    H5Sclose(filespace);
	    H5Sclose(memspace);

	    delete posArray;
            
            sid = H5Screate(H5S_SCALAR);	
            aid = H5Acreate(did, "every", H5T_NATIVE_UINT, sid, H5P_DEFAULT, H5P_DEFAULT);
            H5Awrite(aid, H5T_NATIVE_UINT, &every[np]);
            H5Sclose(sid);
            H5Aclose(aid);
            
            sid = H5Screate(H5S_SCALAR);	
            aid = H5Acreate(did, "dimension", H5T_NATIVE_UINT, sid, H5P_DEFAULT, H5P_DEFAULT);
            H5Awrite(aid, H5T_NATIVE_UINT, &diagParams.probeStruc[np].dim);
            H5Sclose(sid);
            H5Aclose(aid);
            
	    H5Gclose(did);
        }
        
    }
}


DiagnosticProbe::~DiagnosticProbe()
{
    for ( int np=0 ; np < probesArray.size() ; np++ )
	delete probesArray[np];

}


void DiagnosticProbe::close() {
    if (fileId>0) {
        H5Fclose(fileId);
    }
}

string DiagnosticProbe::probeName(int p) {
    ostringstream prob_name("");
    prob_name << "p" << setfill('0') << setw(4) << p;
    return prob_name.str();
}

void DiagnosticProbe::run(unsigned int timestep, ElectroMagn* EMfields, Interpolator* interp) {
    for (unsigned int np=0; np<every.size(); np++) {
        if (every[np] && timestep % every[np] == 0) {

            
            for (int iprob=0; iprob <probeParticles[np].size(); iprob++) {               
                
		(*interp)(EMfields,probeParticles[np],iprob,&Eloc_fields,&Bloc_fields,&Jloc_fields,&probesArray[np]->data_2D[iprob][probeSize-1]);
                    
		//! here we fill the probe data!!!
		probesArray[np]->data_2D[iprob][0]=Eloc_fields.x;
		probesArray[np]->data_2D[iprob][1]=Eloc_fields.y;
		probesArray[np]->data_2D[iprob][2]=Eloc_fields.z;
		probesArray[np]->data_2D[iprob][3]=Bloc_fields.x;
		probesArray[np]->data_2D[iprob][4]=Bloc_fields.y;
		probesArray[np]->data_2D[iprob][5]=Bloc_fields.z;
		probesArray[np]->data_2D[iprob][6]=Jloc_fields.x;
		probesArray[np]->data_2D[iprob][7]=Jloc_fields.y;
		probesArray[np]->data_2D[iprob][8]=Jloc_fields.z;
                
            }

	    // memspace OK : 1 block 
            hsize_t     chunk_parts[2];
            chunk_parts[0] = probeParticles[np].size();
            chunk_parts[1] = probeSize; 
	    hid_t memspace  = H5Screate_simple(2, chunk_parts, NULL);
	    // filespace :
	    hsize_t dimsf[2], offset[2], stride[2], count[2];
	    dimsf[0] = nProbeTot;
	    dimsf[1] = probeSize;
            hid_t filespace = H5Screate_simple(2, dimsf, NULL);
            offset[0] = probesStart[np];
            offset[1] = 0;
            stride[0] = 1;
            stride[1] = 1;
            count[0] = 1;
            count[1] = 1;
            hsize_t     block[2];
            block[0] = probeParticles[np].size();
            block[1] = probeSize;
            H5Sselect_hyperslab(filespace, H5S_SELECT_SET, offset, stride, count, block);

	    // define filespace, memspace
	    hid_t write_plist = H5Pcreate(H5P_DATASET_XFER);
	    H5Pset_dxpl_mpio(write_plist, H5FD_MPIO_INDEPENDENT);
	    hid_t did = H5Gopen2(fileId, probeName(np).c_str(), H5P_DEFAULT);
            hid_t plist_id = H5Pcreate(H5P_DATASET_CREATE);
	    ostringstream name_t;
	    name_t.str("");
	    name_t << "/" << probeName(np).c_str() << "/" << setfill('0') << setw(10) << timestep;
	    hid_t dset_id  = H5Dcreate(did, name_t.str().c_str(), H5T_NATIVE_DOUBLE, filespace, H5P_DEFAULT, plist_id, H5P_DEFAULT);
	    H5Pclose(plist_id);
	    H5Dwrite( dset_id, H5T_NATIVE_DOUBLE, memspace, filespace, write_plist, &(probesArray[np]->data_2D[0][0]) );
	    H5Dclose(dset_id);
	    H5Gclose(did);
	    H5Pclose( write_plist );

	    H5Sclose(filespace);
	    H5Sclose(memspace);

        }
    }
    if (fileId) H5Fflush(fileId, H5F_SCOPE_GLOBAL );
}
