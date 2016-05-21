-- parameter file
------ Size of the simulation -------- 

-- For Testing
nc = 128
boxsize = 100.0

-------- Time Sequence ----
-- linspace: Uniform time steps in a
-- time_step = linspace(0.025, 1.0, 39)
-- logspace: Uniform time steps in loga
-- time_step = linspace(0.01, 1.0, 10)
time_step = {0.02}

output_redshifts= {49.0}  -- redshifts of output

-- Cosmology --
omega_m = 0.29
h       = 0.7


------- Initial power spectrum --------
-- Use the same standard running power law model for the primordial super-
-- horizon power spectrum of curvature perturbations as CAMB, i.e.
--
--   k^3 P_zeta(k) / (2 pi^2) = A_s (k/k_pivot)^(n_s-1),
--
-- where A_s is the amplitude at the pivot scale k=k_pivot. The power of the
-- primordial potential Phi is therefore
-- 
--   P_Phi(k) = (9/25) P_zeta(k)
--            = (9/25) (2 pi^2 / k^3) A_s (k/k_pivot)^(n_s-1).

-- Amplitude of primordial power spectrum at pivot scale
scalar_amp = 2.130624e-9   -- same as scalar_amp in CAMB
-- Pivot scale k_pivot in 1/Mpc
pivot_scalar = 0.05  -- same as pivot_scalar in CAMB
-- Primordial spectral index n_s
scalar_spectral_index = 0.9653  -- same as scalar_spectral_index in CAMB


-- Start with a power spectrum file
-- Linear power spectrum at z=0: k P(k) in Mpc/h units
-- Must be compatible with the Cosmology parameter
read_powerspectrum = "planckDR2_5may2016b_matterpower.dat"
random_seed = 100

-------- Approximation Method ---------------
force_mode = "pm"

pm_nc_factor = {1,   2,   3, }            -- Particle Mesh grid pm_nc_factor*nc per dimension in the beginning
change_pm =    {0, 0.2, 0.5, }            -- time(scaling factor) when the pm_nc_factor is changed, range from 0 to 1

np_alloc_factor= 4.0      -- Amount of memory allocated for particle
loglevel=0                 -- 0=verbose increase value to reduce output msgs

-------- Output ---------------

-- Dark matter particle outputs (all particles)
write_runpb_snapshot= "ic/snp"       -- comment out to suppress snapshot output
-- 1d power spectrum (raw), without shotnoise correction
write_powerspectrum = "ic/powerspec"
write_noisek = "ic/noisek"
write_noise = "ic/noise"