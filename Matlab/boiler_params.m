% =========================================================================
% boiler_params.m — single source of truth for all scenarios
% =========================================================================
%% Water & tank
Rho_Water = 1000;
Cp_Water  = 4184;             % must match Tank_Internal Cp
Water_Volume_L = 150;
Water_Mass_kg  = Water_Volume_L;

%% Internal tank heater
P_Internal_W = 2500;
h_Internal   = 1000;          % validated: removes convective bottleneck
A_Internal   = 1.6;

%% Inline booster — Real-world Andetong JDU30 Thick Film (3000W / 220V)
P_Boost_W = 3000;             % rated electrical power at 220V
h_Boost   = 3000;            % forced turbulent water convection [W/(m^2*K)]
A_Boost   = 0.011;          % internal active contact surface area [m^2]  -> UA = 33 W/K
T_Sheath_Limit_C = 250;     % thick-film element safe surface limit (approx)

%% Losses & grid
UA_Losses = 1.5;              % W/K  (Rth = 0.6667 K/W)
V_Supply  = 230;             % V RMS, Israel single-phase

%% Temperatures & setpoints
T_Initial_C     = 20;
T_Ambient_C     = 20;
T_Water_Inlet_C = 20;
T_target_logic  = 40;         % active comfort target
T_solar         = 20;
T_standby       = 30;
T_dumb_target   = 65;         % conventional thermostat
T_Comfort_C     = 38;         % usable hot-water threshold

%% Safety
T_Max_Cutoff_C = 85;
T_Warn_C       = 80;
Flow_Min_LPM   = 1;
Solar_Gain_Coeff = 8;

%% Fallback aliases (prevent unmapped Simulink blocks defaulting to 0)
T_inlet = T_Water_Inlet_C;   % 20
T_cold  = T_Water_Inlet_C;   % 20
V_tank  = Water_Volume_L;    % 150
M_tank  = Water_Mass_kg;     % 150

%% Fault-injection flags (script-side emulation; model untouched)
Inject_Stuck_SSR_Fault = 0;
Inject_Temp_Override   = 0;

%% Housekeeping
if exist('lib','dir'), addpath('lib'); end
if ~exist('figures','dir'), mkdir('figures'); end