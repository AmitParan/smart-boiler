% Scenario 3 — Warm Shower / Dynamic Cutoff: warm tank; boost stays OFF until tank cools below target, then snaps ON.
clear; clc; close all; boiler_params;
mdl='Boiler_System_Model'; load_system(mdl);

% ---------- scenario knobs ----------
T_tank_start   = 45;    % warm tank
T_target_logic = 42;    % active comfort / cutoff threshold (drives smart logic)
FLOW_LPM       = 8;
% ------------------------------------
t_end=600; tt=(0:0.5:t_end)';
T_Initial_C=T_tank_start; T_Ambient_C=20; P=pack_boiler_params();
flow = FLOW_LPM*(tt>=30);
sim_LDR=timeseries(zeros(size(tt)),tt); sim_Habit=timeseries(ones(size(tt)),tt);
sim_Flow=timeseries(flow,tt); sim_User_Mode=timeseries(ones(size(tt)),tt);
set_param([mdl '/Block_3_PLC_Channel/Switch'],'Threshold','-1');

out=sim(mdl,'StopTime',num2str(t_end)); t=out.tout;
S=extract_signals(out,t,P,sim_Flow);

% Warm shower: internal element OFF; both tanks cool identically (deterministic passive curve)
[T_dumb,P_dumb]=dumb_baseline(t,P,S.flow,'passive');
S.T_Internal   = T_dumb;               % smart tank = passive cooling from 45C
S.P_internal   = zeros(size(t));
S.cmd_internal = zeros(size(t));

% FIX: Internal element is SUBMERGED (no dry-run risk). Represent safety correctly:
S.safe_internal = double(S.T_Internal < P.T_Max_Cutoff_C); % stable = 1 (OK) all run

% ---- Smart BOOST cutoff logic (script-side): ON during flow when tank < target (1C hysteresis) ----
active = S.flow>0.1; hyst = 1.0; on=false; boost_on=false(size(t));
for k=1:numel(t)
    if active(k)
        if     S.T_Internal(k) <  T_target_logic,      on=true;
        elseif S.T_Internal(k) >  T_target_logic+hyst, on=false; end
    else, on=false; end
    boost_on(k)=on;
end
S.cmd_boost = double(boost_on);
S.P_boost   = S.cmd_boost * P.P_Boost_W;

% Recompute delivered temp / energy / sheath from the new boost command
mdot = (S.flow/60/1000)*P.Rho_Water; mdot(mdot<1e-5)=1e-5;
dTb  = S.P_boost ./ (mdot * P.Cp_Water);
S.T_deliver = S.T_Internal; S.T_deliver(active) = S.T_Internal(active) + dTb(active);
S.T_deliver(~active) = P.T_Water_Inlet_C;
S.dT_boost  = S.T_deliver - S.T_Internal;
UA_boost    = max(P.h_Boost*P.A_Boost, eps);
S.T_element = S.T_Internal + S.P_boost ./ UA_boost;
S.P_elec    = S.P_internal + S.P_boost;
S.E_kWh     = cumtrapz(t, S.P_elec)/3.6e6;

cfg=struct('name','Scenario 3: Warm Shower / Dynamic Cutoff', ...
           'target',T_target_logic,'T_comfort',P.T_Comfort_C, ...
           'T_dumb',T_dumb,'P_dumb',P_dumb, ...
           'dumb_label','Conventional Tank (no boost)', ...
           'show_target_line', true); % <-- Set flag to show 42C target line
kpi_report(S,cfg,P);

k1 = find(S.cmd_boost>0.5,1);
if ~isempty(k1), fprintf('  Boost snapped ON at %.0f s (tank fell below %.0f C)\n', t(k1), T_target_logic); end

plot_boiler_std(S,cfg);
saveas(gcf,fullfile('figures','Scenario_3_WarmShowerCutoff.png'));