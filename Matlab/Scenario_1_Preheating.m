% Scenario 1 — Pre-Heating: stagnant 150 L heated to 40C. Smart vs Dumb(65C).
clear; clc; close all; boiler_params;

% Load the core Simulink system model
mdl = 'Boiler_System_Model'; 
load_system(mdl);

% Define simulation time window and initial thermodynamic conditions
t_end = 12000;                                   
tt = (0:1:t_end)';
T_Initial_C = 20; 
T_Ambient_C = 20;             
P = pack_boiler_params();

% Configure simulation input timeseries signals for normal lifecycle run
sim_LDR       = timeseries(zeros(size(tt)),tt); 
sim_Habit     = timeseries(ones(size(tt)),tt);  
sim_Flow      = timeseries(zeros(size(tt)),tt); 
sim_User_Mode = timeseries(ones(size(tt)),tt);  

% Set PLC channel threshold to -1 to guarantee 0% packet loss for clean thermodynamic evaluation
set_param([mdl '/Block_3_PLC_Channel/Switch'],'Threshold','-1');  

% Execute the Simulink model simulation
out = sim(mdl,'StopTime',num2str(t_end));
t = out.tout;

% Extract execution signals and calculate the conventional 65C dumb baseline
S = extract_signals(out, t, P, sim_Flow);
[T_dumb, P_dumb] = dumb_baseline(t, P);         

% Build the configuration structure for standard plotting utilities
cfg = struct('name','Scenario 1: Pre-Heating','target',P.T_target_logic, ...
    'T_comfort',P.T_Comfort_C,'T_dumb',T_dumb,'P_dumb',P_dumb);

% Generate and print the numerical KPI report to the command window
kpi_report(S, cfg, P);

% Render the standard 4-row synchronized performance visualization
plot_boiler_std(S, cfg);

% Post-Processing Graphic Mod: Find and delete the Safety Relay object by its exact DisplayName
h_safe = findobj(gcf, 'DisplayName', 'Safety Relay (1=OK)');
if ~isempty(h_safe)
    delete(h_safe);
end

% Re-align the legend on the 4th subplot to reflect only active switching actuators
subplot(4,1,4);
legend({'Internal Element SSR', 'Inline Boost SSR'}, 'Location', 'east', 'FontSize', 8);

% Verify target directory existence and save the updated high-resolution figure
if ~exist('figures', 'dir')
    mkdir figures; 
end
saveas(gcf, fullfile('figures','Scenario_1_Preheating.png'));