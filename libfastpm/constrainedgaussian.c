#include <math.h>
#include <stdlib.h>

#include <fastpm/libfastpm.h>
#include <fastpm/logging.h>
#include <fastpm/constrainedgaussian.h>

#include "pmpfft.h"
#include <gsl/gsl_linalg.h>

double fastpm_2pcf_eval(FastPM2PCF* self, double r)
{
    return 1.0;
}

/*
void
fastpm_generate_covariance_matrix(PM *pm, fastpm_fkfunc pkfunc, void * data, FastPMFloat *cov_x)
{
}
*/
void
fastpm_2pcf_from_powerspectrum(FastPM2PCF *self, fastpm_fkfunc pkfunc, void * data)
{
}

static void
_solve(int size, double * Cij, double * dfi, double * x)
{


}

void
fastpm_cg_induce_correlation(FastPMConstrainedGaussian *cg, PM * pm, FastPM2PCF *xi, FastPMFloat * delta_k)
{
    double dfi[cg->size];
    double e[cg->size];
    double Cij[cg->size * cg->size];

    FastPMFloat * delta_x = pm_alloc(pm);

    pm_assign(pm, delta_k, delta_x);
    pm_c2r(pm, delta_x);

    int i;
    for(i = 0; i < cg->size; ++i)
    {
        int ii[3];
        int d;
        int inBox = 1;
        int index = 0;
        for(d = 0; d < 3; ++d)
        {
            ii[d] = cg->constraints[i].x[d] * pm->InvCellSize[d] - pm->IRegion.start[d];
            if(ii[d] < 0 || ii[d] > pm->IRegion.size[d])
                inBox = 0;
            index += ii[d] * pm->IRegion.strides[d];
        }

        if(inBox) {
            dfi[i] = cg->constraints[i].c - delta_x[index];
        } else {
            dfi[i] = 0;
        }
    }
    MPI_Allreduce(MPI_IN_PLACE, dfi, cg->size, MPI_DOUBLE, MPI_SUM, pm_comm(pm));

    for(i = 0; i < cg->size; ++i) 
    {
        int j;
        for(j = i; j < cg->size; ++j) 
        {
            int d;
            double r = 0;
            for(d = 0; d < 3; ++d) {
                double dx = cg->constraints[i].x[d] - cg->constraints[j].x[d];
                r += dx * dx;
            }
            r = sqrt(r);
            double v = fastpm_2pcf_eval(xi, r);
            Cij[i * cg->size + j] = v;
            Cij[j * cg->size + i] = v;
        }
    }

    _solve(cg->size, Cij, dfi, e);

    PMXIter xiter;
    for(pm_xiter_init(pm, &xiter);
       !pm_xiter_stop(&xiter);
        pm_xiter_next(&xiter))
    {
        double v = 0;
        for(i = 0; i < cg->size; ++i)
        {
            int d;
            
            double r = 0;
            for(d = 0; d < 3; d ++) {
                double dx = xiter.iabs[d] * pm->CellSize[d] - cg->constraints[i].x[d];
                r += dx * dx;
            }
            r = sqrt(r);

            v += e[i] * fastpm_2pcf_eval(xi, r);
        }
        delta_x[xiter.ind] += v;
    }

    pm_r2c(pm, delta_x, delta_k);
    pm_free(pm, delta_x);
}