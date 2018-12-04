#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <alloca.h>
#include <mpi.h>
#include <math.h>

#include <gsl/gsl_integration.h>
#include <gsl/gsl_roots.h>
#include <gsl/gsl_sf_hyperg.h> 
#include <gsl/gsl_errno.h>
#include <gsl/gsl_math.h>

#include <gsl/gsl_odeiv2.h>
#include <gsl/gsl_spline.h>

#include <fastpm/libfastpm.h>
#include <fastpm/logging.h>

//include Fermi-Dirac integration table fro neutrinos
#include <fastpm/Ftable.h>  //""
size_t Fsize = sizeof(Ftable[0])/sizeof(Ftable[0][0]);                  //maybe include this line in the Ftable.c/h. Would need to include modeule for size_t defn.

double HubbleDistance = 2997.92458; /* Mpc/h */   /*this c*1e5 in SI units*/
double HubbleConstant = 100.0; /* Mpc/h / km/s*/     //OTHER WAY ROUND!

#define STEF_BOLT 2.851e-48  // in units: [ h * (10^10Msun/h) * s^-3 * K^-4 ]
#define rho_crit 27.7455     //rho_crit0 in mass/length (not energy/length)
#define LIGHT 9.722e-15      // in units: [ h * (Mpc/h) * s^-1 ]      //need to update all these units and change Omega_g

#define kB 8.617330350e-5    //boltzman in eV/K   i think these units work best as we'll define neutrino mass in eV

/*Define FastPMCosmology Class for Nu code.
Had to change the syntax comapred to cosmology.h version use typedef and put name at end.
This removes error
*/
typedef struct {
    double h;
    double Omega_cdm;
    double Omega_Lambda;
    double T_cmb;    /*related to omegaR*/
    double N_eff;  //N_ur;      /*this is N_eff*/ //actually i might just make this number o fmassless neutrinos, just adds to rad
    double M_nu; ///    m_ncdm[3]; for now assume 3 nus of same mass
    int N_nu;  //N_ncdm;
} FastPMCosmologyNu;


/*NEW FUNCS ONLY NEEDED FOR NEUTRINOS*/
double Omega_g(FastPMCosmologyNu* c)   
{
    /* This is really Omega_g0. All Omegas in this code mean 0, exceot nu!!, I would change notation, but anyway 
    Omega_g0 = 4 \sigma_B T_{CMB}^4 8 \pi G / (3 c^3 H0^2)*/
    return 4 * STEF_BOLT * pow(c->T_cmb, 4) / pow(LIGHT, 3) / rho_crit / pow(c->h, 2);
}

double HubbleEaNu(double a, FastPMCosmologyNu * c);   //will add to header eventually, or just change func order.

double Omega_cdm_a(double a, FastPMCosmologyNu * c)
{
    //as a func of a. I dont like this. I would use 0s in stryuct instead. ask yu.
    double E = HubbleEaNu(a, c);
    return c->Omega_cdm / (a*a*a) / (E*E);
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
/*ALL FUNCS COPIED FROM COSMOLOGY.C, REPEATED WITH THE VERSION NEEDED FOR NU*/


double HubbleEa(double a, FastPMCosmology * c)
{
    /* H(a) / H0 */
    return sqrt(c->OmegaM/(a*a*a)+c->OmegaLambda);
}

double interpolate(const double xa[], const double ya[], size_t size, double xi)
{
    /*
    Linearly interpolate at xi from data xa[] and ya[]
    */
    
    gsl_interp* interp 
        = gsl_interp_alloc(gsl_interp_linear, size);  //linear
    gsl_interp_accel* acc 
        = gsl_interp_accel_alloc();
    
    gsl_interp_init(interp, xa, ya, size);
    
    double yi = gsl_interp_eval(interp, xa, ya, xi, acc);
    
    gsl_interp_free(interp);
    gsl_interp_accel_free (acc);
    
    return yi;
}

double getFtable(int F_id, double y)
{
    //Not using size as an arg, it's globally defined
    //Gets the interpolated value, unless y=0 for which result is hard coded
    //F_id: 1 for F, 2 for F', 3 for F''
    //F at y means interp at x ... kind of confusing mixing x and y, but whatever.
    
    if (y == 0.) {               //does this work for double?
        if (F_id == 1) {
            return 5.6822;    //check
        }else{
            return 0.;
        }
    }else{
        return interpolate(Ftable[0], Ftable[F_id], Fsize, y);
    }
}

double Fconst(FastPMCosmologyNu * c)
{
    /*
    This is a cosmology dependent constant which is the argument divided by a of F, DF, DDF used repeatedly in code
    To be clear, evaluate F at Fconst*a
    */
    
    if (c->T_cmb == 0.){
        return 0.;                 //really get infinity, but no gamma means no nu, so effectively F_const=0. Exact value doesnt matter, because the Omega_g factor in Omega_nu will make it 0 regarldess of  what F gives, just need to avoif nan.
    }else{
        double Gamma_nu = pow(c->N_eff / c->N_nu, 1./4.) * pow( 4./11., 1./3.);   //nu to photon temp ratio today
        double T_nu = Gamma_nu * c->T_cmb;
        return c->M_nu / c->N_nu / (kB * T_nu);
    }
}


double OmegaNuTimesHubbleEaSq(double a, FastPMCosmologyNu * c)   
{
    /* Use interpolation to find Omega_nu(a) * E(a)^2 */
    
    double Fc = Fconst(c);
    double F = getFtable(1, Fc*a);    //row 1 for F
    double A = 15. / pow(M_PI, 4) * pow(4./11., 4./3.) * c->N_eff * Omega_g(c);
    
    return A / (a*a*a*a) * F;
}

//lots of repn in the below funcs, should we define things globally (seems messy), or object orient?
double DOmegaNuTimesHubbleEaSqDa(double a, FastPMCosmologyNu * c)
{
    double Fc = Fconst(c);
    double DF = getFtable(2, Fc*a);   //row 2 for F'
    double A = 15. / pow(M_PI, 4) * pow(4./11., 4./3.) * c->N_eff * Omega_g(c);
    
    double OnuESq = OmegaNuTimesHubbleEaSq(a,c);
    
    return -4. / a * OnuESq + A / (a*a*a*a) * Fc * DF;
}

double D2OmegaNuTimesHubbleEaSqDa2(double a, FastPMCosmologyNu * c)
{
    double Fc = Fconst(c);
    double DDF = getFtable(3, Fc*a);   //row 3 for F''
    double A = 15. / pow(M_PI, 4) * pow(4./11., 4./3.) * c->N_eff * Omega_g(c);
    
    double OnuESq = OmegaNuTimesHubbleEaSq(a,c);
    double DOnuESqDa = DOmegaNuTimesHubbleEaSqDa(a,c);
    
    return -12. / (a*a) * OnuESq - 8. / a * DOnuESqDa + A / (a*a*a*a) * (Fc*Fc) * DDF;
}

double HubbleEaNu(double a, FastPMCosmologyNu * c)
{
    /* H(a) / H0 */
    return sqrt(Omega_g(c) / (a*a*a*a) + c->Omega_cdm / (a*a*a) + OmegaNuTimesHubbleEaSq(a, c) + c->Omega_Lambda);
}

double DHubbleEaDaNu(double a, FastPMCosmologyNu * c)
{
    /* d E / d a*/
    double E = HubbleEaNu(a, c);
    double DOnuESqDa = DOmegaNuTimesHubbleEaSqDa(a,c);
    
    return 0.5 / E * ( - 4 * Omega_g(c) / pow(a,5) - 3 * c->Omega_cdm / pow(a,4) + DOnuESqDa );
}

double D2HubbleEaDa2Nu(double a, FastPMCosmologyNu * c)
{
    double E = HubbleEaNu(a,c);
    double dEda = DHubbleEaDaNu(a,c);
    double D2OnuESqDa2 = D2OmegaNuTimesHubbleEaSqDa2(a,c);

    return 0.5 / E * ( 20 * Omega_g(c) / pow(a,6) + 12 * c->Omega_cdm / pow(a,5) + D2OnuESqDa2 - 2 * pow(dEda,2) );
}

double OmegaSum(double a, FastPMCosmologyNu* c)
{
    //should always equal 1. good for testing.
    double sum = Omega_g(c) / pow(a, 4);
    sum += c->Omega_cdm / pow(a, 3);
    sum += OmegaNuTimesHubbleEaSq(a, c);
    sum += c->Omega_Lambda;
    return sum / pow(HubbleEaNu(a, c), 2);
}

static double growth_int(double a, void *param)
{
    double * p = (double*) param;
    double OmegaM = p[0];
    double OmegaLambda = p[1];
    return pow(a / (OmegaM + (1 - OmegaM - OmegaLambda) * a + OmegaLambda * a * a * a), 1.5);
}  //the above assumes that there is no radiation, and any 'leftover' Omega is due to curvature.

static double growth(double a, FastPMCosmology * c)
{
    /* NOTE that the analytic COLA growthDtemp() is 6 * pow(1 - c.OmegaM, 1.5) times growth() */

    int WORKSIZE = 100000;

    double result, abserr;
    gsl_integration_workspace *workspace;
    gsl_function F;

    workspace = gsl_integration_workspace_alloc(WORKSIZE);

    F.function = &growth_int;
    F.params = (double[]) {c->OmegaM, c->OmegaLambda};

    gsl_integration_qag(&F, 0, a, 0, 1.0e-9, WORKSIZE, GSL_INTEG_GAUSS41, 
            workspace, &result, &abserr);

    gsl_integration_workspace_free(workspace);

    return HubbleEa(a, c) * result;
}

static int growth_odeNu(double a, const double yy[], double dyda[], void *params)
{
    //is yy needed?
    FastPMCosmologyNu* c = (FastPMCosmologyNu * ) params;
    
    const double E = HubbleEaNu(a, c);
    const double dEda = DHubbleEaDaNu(a, c);
    
    double dydlna[4];
    dydlna[0] = yy[1];
    dydlna[1] = - (2. + a / E * dEda) * yy[1] + 1.5 * Omega_cdm_a(a, c) * yy[0];
    dydlna[2] = yy[3];
    dydlna[3] = - (2. + a / E * dEda) * yy[3] + 1.5 * Omega_cdm_a(a, c) * (yy[2] - yy[0]*yy[0]);
    
    //divide by  a to get dyda
    for (int i=0; i<4; i++){
        dyda[i] = dydlna[i] / a;
    }
    
    return GSL_SUCCESS;
}

typedef struct{
    double y0;
    double y1;
    double y2;
    double y3;
} ode_soln;

static ode_soln growth_ode_solveNu(double a, FastPMCosmologyNu * c)
{
    /* NOTE that the analytic COLA growthDtemp() is 6 * pow(1 - c.OmegaM, 1.5) times growth() */
    /* This returns an array of {D, dD/da}
        Is there a nicer way to do this within one func and no object?
        I'm still going to define 2 functions, one for D and one for dD/da, for neatness, but it's messy. Ask Yu*/

    gsl_odeiv2_system FF;
    FF.function = &growth_odeNu;
    FF.jacobian = NULL;
    FF.dimension = 4;
    FF.params = (void*) c;
    
    gsl_odeiv2_driver * drive 
        = gsl_odeiv2_driver_alloc_standard_new(&FF,
                                               gsl_odeiv2_step_rkf45, 
                                               1e-5, 
                                               1e-8,
                                               1e-8,
                                               1,
                                               1);
    
     /* We start early to avoid lambda. ??????????????*/
    double aini = 1e-5;
    
    //Note the normalisation of D is arbitrary as we will only use it to calcualte growth fractor.
    
    //MD initial conditions for now.
    double yini[4] = {aini, aini, -3./7.*aini*aini, -6./7.*aini*aini};
    //double yini[4] = {1e7*aini, 1e52*aini, -3./7.*aini*aini*1e97, -6./7.*aini*aini/1e50};
   
    //int stat = 
    gsl_odeiv2_driver_apply(drive, &aini, a, yini);
    //if (stat != GSL_SUCCESS) {     //If succesful, stat will = GSL_SUCCESS and yinit will become the final values...
        //endrun(1,"gsl_odeiv in growth: %d. Result at %g is %g %g\n",stat, curtime, yinit[0], yinit[1]); //need to define endrun
    //    printf(stat);    //quick fix for now 
    //}
    
    gsl_odeiv2_driver_free(drive);
    /*Store derivative of D if needed.*/
    //if(dDda) {
    //    *dDda = yinit[1]/pow(a,3)/(hubble_function(a)/CP->Hubble);
    //}
    
    ode_soln soln;
    soln.y0 = yini[0];
    soln.y1 = yini[1];
    soln.y2 = yini[2];
    soln.y3 = yini[3];
    
    return soln;
}

double growthNu(double a, FastPMCosmologyNu * c) {
    //d1
    return growth_ode_solveNu(a, c).y0;
}

double DgrowthDlnaNu(double a, FastPMCosmologyNu * c) {
    //F1
    return growth_ode_solveNu(a, c).y1;
}

double growth2Nu(double a, FastPMCosmologyNu * c) {
    //d2
    return growth_ode_solveNu(a, c).y2;
}

double Dgrowth2DlnaNu(double a, FastPMCosmologyNu * c) {
    //F2
    return growth_ode_solveNu(a, c).y3;
}


double OmegaA(double a, FastPMCosmology * c) {
    return c->OmegaM/(c->OmegaM + (c->OmegaLambda)*a*a*a); 
}

double OmegaANu(double a, FastPMCosmologyNu * c) {
    //this is what I called Omega_cdm_a above. choose which to use later.
    return Omega_cdm_a(a, c);
}

double GrowthFactor(double a, FastPMCosmology * c) { // growth factor for LCDM
    return growth(a, c) / growth(1., c);           //[this is D(a)/D_today for LCDM]
}

double GrowthFactorNu(double a, FastPMCosmologyNu * c) { // growth factor for LCDM
    return growthNu(a, c) / growthNu(1., c);           //[this is D(a)/D_today for LCDM]
}


double DLogGrowthFactor(double a, FastPMCosmology * c) {
    /* Or OmegaA^(5/9) */
    return pow(OmegaA(a, c), 5.0 / 9);
}

double DLogGrowthFactorNu(double a, FastPMCosmologyNu * c) {
    /* dlnD1/dlna */
    double d1 = growthNu(a, c);
    double F1 = DgrowthDlnaNu(a, c);
    
    return F1 / d1;
}

double GrowthFactor2(double a, FastPMCosmology * c) {
    /* Second order growth factor */
    /* 7 / 3. is absorbed into dx2 */
    //ADRIAN: MISSING MINUS SIGN????!
    double d = GrowthFactor(a, c);
    return d * d * pow(OmegaA(a, c) / OmegaA(1.0, c), -1.0/143.);
}

double GrowthFactor2Nu(double a, FastPMCosmologyNu * c) {
    /* Normalised D2. Is this correct???*/
    double d0 = growthNu(1., c);  //0 for today
    return growth2Nu(a, c) / growth2Nu(1., c); //(d0*d0); // ??????????????????;
}

double DLogGrowthFactor2(double a, FastPMCosmology * c) {
    return 2 * pow(OmegaA(a, c), 6.0/11.);
}

double DLogGrowthFactor2Nu(double a, FastPMCosmologyNu * c) {
    /* dlnD2/dlna */
    double d2 = growth2Nu(a, c);
    double F2 = Dgrowth2DlnaNu(a, c);
    
    return F2 / d2;
}



//i feel like these should be moved up, would be nice to define Ea nd its derivs together. ive moved nu stuff up
double DHubbleEaDa(double a, FastPMCosmology * c) {
    /* d E / d a*/
    double E = HubbleEa(a, c);
    return 0.5 / E * (-3 * c->OmegaM / (a * a * a * a));
}

double D2HubbleEaDa2(double a, FastPMCosmology * c) {
    double E = HubbleEa(a, c);
    double dEda = DHubbleEaDa(a, c);
    return - dEda * dEda / E + dEda * (-4 / a);
}




double DGrowthFactorDa(double a, FastPMCosmology * c) {
    double E = HubbleEa(a, c);

    double EI = growth(1.0, c);

    double t1 = DHubbleEaDa(a, c) * GrowthFactor(a, c) / E;
    double t2 = E * pow(a * E, -3) / EI;
    return t1 + t2;
}

double D2GrowthFactorDa2(double a, FastPMCosmology * c) {
    double d2Eda2 = D2HubbleEaDa2(a, c);
    double dEda = DHubbleEaDa(a, c);
    double E = HubbleEa(a, c);
    double EI = growth(1.0, c);
    double t1 = d2Eda2 * GrowthFactor(a, c) / E;
    double t2 = (dEda + 3 / a * E) * pow(a * E, -3) / EI;
    return t1 - t2;
}

double DGrowthFactorDaNu(double a, FastPMCosmologyNu * c) {
    double d0 = growthNu(1., c);
    double F1 = DgrowthDlnaNu(a, c);
    return F1 / a / d0;
}

double D2GrowthFactorDa2Nu(double a, FastPMCosmologyNu * c) {
    double E = HubbleEaNu(a, c);
    double dEda = DHubbleEaDaNu(a, c);
    
    double d1 = growthNu(a, c);
    double F1 = DgrowthDlnaNu(a, c);
    double d0 = growthNu(1., c);  //0 for today
    
    double ans = 0.;
    ans -= (3. + a / E * dEda) * F1;
    ans += 1.5 * Omega_cdm_a(a, c) * d1;
    ans /= d0 * a*a;
    return ans;
}




static double
comoving_distance_int(double a, void * params)
{
    FastPMCosmology * c = (FastPMCosmology * ) params;
    return 1 / (a * a * HubbleEa(a, c));
}

/* In Hubble Distance */
double ComovingDistance(double a, FastPMCosmology * c) {

    /* We tested using ln_a doesn't seem to improve accuracy */

    int WORKSIZE = 100000;

    double result, abserr;
    gsl_integration_workspace *workspace;
    gsl_function F;

    workspace = gsl_integration_workspace_alloc(WORKSIZE);

    F.function = &comoving_distance_int;
    F.params = (void*) c;

    gsl_integration_qag(&F, a, 1, 0, 1.0e-9, WORKSIZE, GSL_INTEG_GAUSS41,
            workspace, &result, &abserr);

    gsl_integration_workspace_free(workspace);

    return result;
}

static double
comoving_distance_intNu(double a, void * params)
{
    FastPMCosmologyNu * c = (FastPMCosmologyNu * ) params;
    return 1. / (a * a * HubbleEaNu(a, c));
}

double ComovingDistanceNu(double a, FastPMCosmologyNu * c) {

    /* We tested using ln_a doesn't seem to improve accuracy */

    int WORKSIZE = 100000;

    double result, abserr;
    gsl_integration_workspace *workspace;
    gsl_function F;

    workspace = gsl_integration_workspace_alloc(WORKSIZE);

    F.function = &comoving_distance_intNu;
    F.params = (void*) c;

    gsl_integration_qag(&F, a, 1., 0, 1.0e-8, WORKSIZE, GSL_INTEG_GAUSS41,
            workspace, &result, &abserr); 
            //lowered tol by /10 to avoid error from round off (maybe I need to make my paras more accurate)

    gsl_integration_workspace_free(workspace);

    return result;
}

void
fastpm_horizon_init(FastPMHorizon * horizon, FastPMCosmology * cosmology)
{
    gsl_set_error_handler_off(); // Turn off GSL error handler

    horizon->cosmology = cosmology;
    horizon->size = 8192;
    horizon->da = 1.0 / (horizon->size - 1);
    int i;
    for (i = 0; i < horizon->size; i ++) {
        double a = 1.0 * i / (horizon->size - 1);
        horizon->xi_a[i] = HubbleDistance * ComovingDistance(a, horizon->cosmology);
        horizon->growthfactor_a[i] = GrowthFactor(a, horizon->cosmology);
    }
}

void
fastpm_horizon_initNu(FastPMHorizon * horizon, FastPMCosmologyNu * cosmology)
{
    gsl_set_error_handler_off(); // Turn off GSL error handler

    horizon->cosmology = cosmology;
    horizon->size = 8192;
    horizon->da = 1.0 / (horizon->size - 1);
    int i;
    for (i = 0; i < horizon->size; i ++) {
        double a = 1.0 * i / (horizon->size - 1);
        horizon->xi_a[i] = HubbleDistance * ComovingDistanceNu(a, horizon->cosmology);
        horizon->growthfactor_a[i] = GrowthFactorNu(a, horizon->cosmology);
    }
}


/*~~~~~~~~~~~~~BELOW SEEM COSMOLOGY (AND => NU) INDEP IN TERMS OF FUNC DEFN~~~~~~~~~~~~~~~~~~~~~*/
void
fastpm_horizon_destroy(FastPMHorizon * horizon)
{
}

void
fastpm_horizon_destroyNu(FastPMHorizon * horizon)
{
}

double
HorizonDistance(double a, FastPMHorizon * horizon)
{
    double x = a * (horizon->size - 1);
    int l = floor(x);
    int r = l + 1;
    if(r >= horizon->size) {
        return horizon->xi_a[horizon->size - 1];
    }
    if(l <= 0) {
        return horizon->xi_a[0];
    }
    return horizon->xi_a[l] * (r - x)
         + horizon->xi_a[r] * (x - l);
}

double
HorizonDistanceNu(double a, FastPMHorizon * horizon)
{
    double x = a * (horizon->size - 1);
    int l = floor(x);
    int r = l + 1;
    if(r >= horizon->size) {
        return horizon->xi_a[horizon->size - 1];
    }
    if(l <= 0) {
        return horizon->xi_a[0];
    }
    return horizon->xi_a[l] * (r - x)
         + horizon->xi_a[r] * (x - l);
}

double
HorizonGrowthFactor(double a, FastPMHorizon * horizon)
{
    double x = a * (horizon->size - 1);
    int l = floor(x);
    int r = l + 1;
    if(r >= horizon->size) {
        return horizon->growthfactor_a[horizon->size - 1];
    }
    if(l <= 0) {
        return horizon->growthfactor_a[0];
    }
    return horizon->growthfactor_a[l] * (r - x)
         + horizon->growthfactor_a[r] * (x - l);
}

double
HorizonGrowthFactorNu(double a, FastPMHorizon * horizon)
{
    double x = a * (horizon->size - 1);
    int l = floor(x);
    int r = l + 1;
    if(r >= horizon->size) {
        return horizon->growthfactor_a[horizon->size - 1];
    }
    if(l <= 0) {
        return horizon->growthfactor_a[0];
    }
    return horizon->growthfactor_a[l] * (r - x)
         + horizon->growthfactor_a[r] * (x - l);
}

void *
fastpm_horizon_solve_start()
{
    const gsl_root_fsolver_type *T = gsl_root_fsolver_brent;
    return gsl_root_fsolver_alloc(T);
}

void *
fastpm_horizon_solve_startNu()
{
    const gsl_root_fsolver_type *T = gsl_root_fsolver_brent;
    return gsl_root_fsolver_alloc(T);
}

void
fastpm_horizon_solve_end(void * context)
{
    gsl_root_fsolver_free(context);
}

void
fastpm_horizon_solve_endNu(void * context)
{
    gsl_root_fsolver_free(context);
}

int
fastpm_horizon_solve(FastPMHorizon * horizon,
    void * context,
    double * solution,
    double a_i, double a_f,
    double (*func)(double a, void * userdata),
    void * userdata)
{

    int status;
    int iter = 0, max_iter;
    double r, x_lo=a_i, x_hi=a_f, eps;

    /* Reorganize to struct later */
    max_iter = 100;
    eps = 1e-7;

    gsl_function F;

    F.function = func;
    F.params = userdata;

    status = gsl_root_fsolver_set(context, &F, x_lo, x_hi);

    if(status == GSL_EINVAL || status == GSL_EDOM) {
        /** Error in value or out of range **/
        return 0;
    }

    do
    {
        iter++;
        //
        // Debug printout #1
        //if(iter == 1) {
        //fastpm_info("ID | [x_lo, x_hi] | r | funct(r) | x_hi - x_lo\n");
        //}
        //

        status = gsl_root_fsolver_iterate(context);
        r = gsl_root_fsolver_root(context);

        x_lo = gsl_root_fsolver_x_lower(context);
        x_hi = gsl_root_fsolver_x_upper(context);

        status = gsl_root_test_interval(x_lo, x_hi, eps, 0.0);
        //
        //Debug printout #2
        //fastpm_info("%5d [%.7f, %.7f] %.7f %.7f %.7f\n", iter, x_lo, x_hi, r, funct(r, &params), x_hi - x_lo);
        //

        if(status == GSL_SUCCESS) {
            *solution = r;
            //
            // Debug printout #3.1
            //fastpm_info("fastpm_lc_intersect() called with parameters %.7f and %.7f, returned status %d.\n\n", a, b, 1);
            //
            return 1;
        }
    }
    while (status == GSL_CONTINUE && iter < max_iter);
    //
    // Debug printout #3.2
    //fastpm_info("fastpm_lc_intersect() called with parameters %.7f and %.7f, returned status %d.\n\n", a, b, 0);
    //

    return 0;

}

int
fastpm_horizon_solveNu(FastPMHorizon * horizon,
    void * context,
    double * solution,
    double a_i, double a_f,
    double (*func)(double a, void * userdata),
    void * userdata)
{

    int status;
    int iter = 0, max_iter;
    double r, x_lo=a_i, x_hi=a_f, eps;

    /* Reorganize to struct later */
    max_iter = 100;
    eps = 1e-7;

    gsl_function F;

    F.function = func;
    F.params = userdata;

    status = gsl_root_fsolver_set(context, &F, x_lo, x_hi);

    if(status == GSL_EINVAL || status == GSL_EDOM) {
        /** Error in value or out of range **/
        return 0;
    }

    do
    {
        iter++;
        //
        // Debug printout #1
        //if(iter == 1) {
        //fastpm_info("ID | [x_lo, x_hi] | r | funct(r) | x_hi - x_lo\n");
        //}
        //

        status = gsl_root_fsolver_iterate(context);
        r = gsl_root_fsolver_root(context);

        x_lo = gsl_root_fsolver_x_lower(context);
        x_hi = gsl_root_fsolver_x_upper(context);

        status = gsl_root_test_interval(x_lo, x_hi, eps, 0.0);
        //
        //Debug printout #2
        //fastpm_info("%5d [%.7f, %.7f] %.7f %.7f %.7f\n", iter, x_lo, x_hi, r, funct(r, &params), x_hi - x_lo);
        //

        if(status == GSL_SUCCESS) {
            *solution = r;
            //
            // Debug printout #3.1
            //fastpm_info("fastpm_lc_intersect() called with parameters %.7f and %.7f, returned status %d.\n\n", a, b, 1);
            //
            return 1;
        }
    }
    while (status == GSL_CONTINUE && iter < max_iter);
    //
    // Debug printout #3.2
    //fastpm_info("fastpm_lc_intersect() called with parameters %.7f and %.7f, returned status %d.\n\n", a, b, 0);
    //

    return 0;

}

int main(int argc, char * argv[]) {

    MPI_Init(&argc, &argv);

    libfastpm_init();

    MPI_Comm comm = MPI_COMM_WORLD;

    fastpm_set_msg_handler(fastpm_default_msg_handler, comm, NULL);

    /* the old COLA growthDtemp is 6 * pow(1 - c.OmegaM, 1.5) times growth */
    FastPMCosmology c[1] = {{
        .OmegaM = 0.3,
        .OmegaLambda = 0.7
    }};

    printf("OmegaM X D dD/da d2D/da2 D2 E dE/dA d2E/da2 f1 f2 \n");
    for(c->OmegaM = 0.1; c->OmegaM < 0.6; c->OmegaM += 0.1) {
        c->OmegaLambda = 1 - c->OmegaM;
        double a = 0.8;
        
        printf("%g %g %g %g %g %g %g %g %g %g %g\n",
            c->OmegaM, 
            ComovingDistance(a, c),
            //growth(a, c),
            GrowthFactor(a, c),
            DGrowthFactorDa(a, c),
            D2GrowthFactorDa2(a, c),

            GrowthFactor2(a, c),
            HubbleEa(a, c),
            DHubbleEaDa(a, c),
            D2HubbleEaDa2(a, c),
               
            DLogGrowthFactor2(a, c),
            DLogGrowthFactor(a, c)
            );
    }
    
    //FOR NUUUUU
    /*understand this piece of code, what is [1]? why {{}}? etc. print cNu maybe?*/ 
    //Initialize cNu. Values not important and will be changed in a sec
    FastPMCosmologyNu cNu[1] ={{
        .h=0.6772,
        .Omega_cdm=0.3,
        .Omega_Lambda=0.7,
        .T_cmb=0.,//2.73,
        .N_eff=3.046,
        .M_nu=0.,               //(assuming 3 nus of mass 1ev, this is the sum of their masses)
        .N_nu=3
    }};
    
    /*Insert a new for loop? Think of how best to output.*/
    printf("NU \n");
    printf("OmegaM X D dD/da d2D/da2 D2 E dE/dA d2E/da2 f1 f2 OmegaG\n");
    for(cNu->Omega_cdm = 0.1; cNu->Omega_cdm < 0.6; cNu->Omega_cdm += 0.1) {
        //cNu->Omega_Lambda = 0;  //initialize it
        cNu->Omega_Lambda = 1 - cNu->Omega_cdm - Omega_g(cNu) - OmegaNuTimesHubbleEaSq(1,cNu);
        double a = 0.8;
        printf("%g %g %g %g %g %g %g %g %g %g %g %g \n",
            cNu->Omega_cdm,
           
            ComovingDistanceNu(a, cNu),
            //growthNu(a, cNu),
            GrowthFactorNu(a, cNu),
            DGrowthFactorDaNu(a, cNu),
            D2GrowthFactorDa2Nu(a, cNu),

            GrowthFactor2Nu(a, cNu),
            HubbleEaNu(a, cNu),
            DHubbleEaDaNu(a, cNu),
            D2HubbleEaDa2Nu(a, cNu),
               
            DLogGrowthFactor2Nu(a, cNu),
            DLogGrowthFactorNu(a, cNu),
               
            Omega_g(cNu)
            //OmegaSum(a, cNu),
            //OmegaSum(1, cNu),
            );
    }

    /*
    double xa[100];
    double ya[100];
    for (int i=0; i<100; i+=1){
        xa[i]=i;
        ya[i]=i;
    }
    printf("%g",interpolate(xa,ya,4.3));
    */
    
    libfastpm_cleanup();
    MPI_Finalize();
    return 0;
}