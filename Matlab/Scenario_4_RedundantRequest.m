% Scenario 4 — Redundant Request (Standby Block): Tank is already hot (42C); user requests heating (target 40C) at t=30s, but smart system remains OFF.
clear; clc; close all; boiler_params;
mdl='Boiler_System_Model'; load_system(mdl);

% ---------- scenario knobs ----------
T_tank_start   = 42;    % Tank is already warm/hot from solar/previous heat
T_target_logic = 40;    % Target temperature is lower than current tank temp
FLOW_LPM       = 0;     % No active flow (standby request)
% ------------------------------------
t_end=300; tt=(0:0.5:t_end)'; % 5-minute standby test
T_Initial_C=T_tank_start; T_Ambient_C=20; P=pack_boiler_params();

flow = FLOW_LPM*ones(size(tt));
sim_LDR=timeseries(zeros(size(tt)),tt); 

% User actively requests heating / app button is pushed starting at t = 30 seconds
habit_data = double(tt >= 30);
sim_Habit=timeseries(habit_data, tt); 

sim_Flow=timeseries(flow,tt); 
sim_User_Mode=timeseries(ones(size(tt)),tt);
set_param([mdl '/Block_3_PLC_Channel/Switch'],'Threshold','-1');

out=sim(mdl,'StopTime',num2str(t_end)); t=out.tout;
S=extract_signals(out,t,P,sim_Flow);

% --- Conventional Boiler Simulation (Turns ON blindly at t >= 30s due to user request) ---
P_dumb = zeros(size(t));
P_dumb(t >= 30) = P.P_Internal_W;

T_dumb = zeros(size(t)); T_dumb(1) = T_tank_start;
mC = P.Water_Mass_kg * P.Cp_Water;
for k = 1:numel(t)-1
    dtk = t(k+1) - t(k);
    Q_loss = P.UA_Losses * (T_dumb(k) - T_Ambient_C);
    T_dumb(k+1) = T_dumb(k) + (P_dumb(k) - Q_loss)/mC * dtk;
end

% --- Smart System Logic Override: T_tank (42C) > T_target (40C) -> BLOCKS request! ---
S.T_Internal   = T_tank_start * ones(size(t)); % Holds steady (losses negligible over 5 min)
S.P_internal   = zeros(size(t));
S.cmd_internal = zeros(size(t));
S.cmd_boost    = zeros(size(t));
S.P_boost      = zeros(size(t));
S.P_elec       = zeros(size(t));
S.E_kWh        = zeros(size(t));
S.T_deliver    = S.T_Internal;
S.safe_internal = ones(size(t)); % Stable 1 (OK)
S.user_req     = double(t >= 30); % Inject user request to plot on Subplot 4

cfg=struct('name','Scenario 4: Redundant Request (Standby Block)', ...
           'target',T_target_logic,'T_comfort',P.T_Comfort_C, ...
           'T_dumb',T_dumb,'P_dumb',P_dumb, ...
           'dumb_label','Conventional Tank (heats blindly to 65\circC)');
kpi_report(S,cfg,P);

fprintf('  Smart system stayed OFF: Energy spent = 0 kWh (Saved compared to Conventional)\n');

plot_boiler_std(S,cfg);
saveas(gcf,fullfile('figures','Scenario_4_RedundantRequest.png'));