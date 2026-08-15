%% Scenario 2 — Preheated Shower: same warm tank for both; only the smart system has the inline boost.
clear; clc; close all; boiler_params;
mdl='Boiler_System_Model'; load_system(mdl);

% ---------- Scenario Tuning Knobs ----------
T_tank_start = 40;   % Smart preheat target temperature [C]
FLOW_LPM     = 8;    % Active shower volumetric flow rate [LPM]
% -------------------------------------------

t_end=600; 
tt=(0:0.5:t_end)';
T_Initial_C=T_tank_start; 
T_Ambient_C=20; 
P=pack_boiler_params();

% Generate flow profile: Step input starting at t = 30 seconds
flow = FLOW_LPM*(tt>=30);

% Configure simulation input timeseries arrays
sim_LDR       = timeseries(zeros(size(tt)),tt); 
sim_Habit     = timeseries(ones(size(tt)),tt);
sim_Flow      = timeseries(flow,tt); 
sim_User_Mode = timeseries(ones(size(tt)),tt);

% Force PLC communication channel to 0% packet loss for nominal operation
set_param([mdl '/Block_3_PLC_Channel/Switch'],'Threshold','-1');

% Run Simulink framework simulation
out=sim(mdl,'StopTime',num2str(t_end)); 
t=out.tout;
S=extract_signals(out,t,P,sim_Flow);

% Conventional boiler baseline: passive reference with zero electrical boost capability
[T_dumb,P_dumb]=dumb_baseline(t,P,S.flow,'passive');

% Structure plot configuration parameters
cfg=struct('name','Scenario 2: Preheated Shower (Smart Preheat + Boost)', ...
           'target',P.T_Comfort_C,'T_comfort',P.T_Comfort_C, ...
           'T_dumb',T_dumb,'P_dumb',P_dumb, ...
           'dumb_label','Conventional Tank (no heat / no boost)');

% Execute numerical KPI evaluation and print console metrics
kpi_report(S,cfg,P);

% Calculate localized command window analytics
active=S.flow>0.1;
comfort_frac = mean(S.T_deliver(active)>=P.T_Comfort_C)*100;
dT_show = max(S.T_deliver(active) - S.T_Internal(active));   

fprintf('  Delivered %.1f..%.1f C | boost dT=%.1f C | comfortable %.0f%% of shower\n', ...
        min(S.T_deliver(active)), max(S.T_deliver(active)), dT_show, comfort_frac);

% Render standard synchronized 4-row performance figure
plot_boiler_std(S,cfg);

% Post-Processing Graphic Mod: Strip out the Safety Relay trace for normal run clarity
h_safe = findobj(gcf, 'DisplayName', 'Safety Relay (1=OK)');
if ~isempty(h_safe)
    delete(h_safe);
end

% Re-adjust the axis legend to map only the active power switching devices
subplot(4,1,4);
legend({'Internal Element SSR', 'Inline Boost SSR'}, 'Location', 'east', 'FontSize', 8);

% Save high-resolution simulation output image
if ~exist('figures','dir'), mkdir figures; end
saveas(gcf,fullfile('figures','Scenario_2_PreheatedShower.png'));