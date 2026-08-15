% Scenario 6 — PLC Communication Loss & Safety Fail-Safe (60s high-res, script-emulated).
% Sequence: State 0 idle -> State 1 active heating -> sudden comm loss -> safety blocks heating.
clear; clc; close all; 

% Load system parameters
if exist('boiler_params.m', 'file')
    boiler_params; 
    P = pack_boiler_params();
else
    % Standalone fallback parameters if boiler_params is not in path
    P.Water_Mass_kg  = 150; 
    P.Cp_Water       = 4184; 
    P.UA_Losses      = 1.5;
    P.P_Internal_W   = 2500; 
    P.T_Ambient_C    = 20; 
    P.T_Initial_C    = 20;
end

% Simulation setup (60 seconds, 50 ms resolution)
t_end    = 60; 
dt       = 0.05; 
t        = (0:dt:t_end)'; 
n        = numel(t);
t_active = 15; % Time [s]: Idle -> Active heating (demand appears)
t_loss   = 30; % Time [s]: PLC link drops mid-operation

% --- Timeline control signals ---
PLC_ok        = double(t < t_loss);                % 1 = Link OK, 0 = Link Lost
Demand        = double(t >= t_active);             % 1 = User/system wants heat, 0 = Idle
Safe_Internal = Demand .* PLC_ok;                  % FAIL-SAFE: heat only if demand AND comm are OK
P_heater      = Safe_Internal * P.P_Internal_W;    % Actual electric heater power [W]

% --- Tank Temperature response ---
mC = P.Water_Mass_kg * P.Cp_Water; 
T  = zeros(size(t)); 
T(1) = P.T_Initial_C;
for k = 1:n-1
    Qloss  = P.UA_Losses * (T(k) - P.T_Ambient_C);
    T(k+1) = T(k) + (P_heater(k) - Qloss)/mC * dt;
end

% Create high-res comparative figure
figure('Name', 'Scenario 6: PLC Comm Loss & Safety', 'Color', 'w', 'Position', [70 40 940 800]);

% ================= Subplot 1: Communication State =================
subplot(3, 1, 1); hold on; grid on;
area(t, double(t >= t_loss), 'BaseValue', 0, 'FaceColor', [1 0.6 0.6], 'FaceAlpha', 0.22, ...
     'EdgeColor', 'none', 'HandleVisibility', 'off');
stairs(t, PLC_ok, 'b-', 'LineWidth', 2.2);
xline(t_loss, 'r--', 'COMM LOST', 'LineWidth', 1.6, ...
      'LabelHorizontalAlignment', 'left', 'LabelVerticalAlignment', 'top', 'FontWeight', 'bold');
ylim([-0.1 1.2]); yticks([0 1]); yticklabels({'LOST (0)', 'OK (1)'}); 
xlim([0 t_end]); ylabel('PLC Link'); 
title('PLC Communication Link Status');
legend('Communication status', 'Location', 'east');

% ================= Subplot 2: Demand vs Safety Interlock =================
subplot(3, 1, 2); hold on; grid on;
yyaxis left;
blocked = (Demand > 0.5) & (Safe_Internal < 0.5);
area(t, double(blocked), 'BaseValue', 0, 'FaceColor', [1 0.6 0.6], 'FaceAlpha', 0.22, ...
     'EdgeColor', 'none', 'HandleVisibility', 'off');
hD = stairs(t, Demand, 'b-', 'LineWidth', 2.2);
hS = stairs(t, Safe_Internal, 'g-', 'LineWidth', 2.2);
ylim([-0.1 1.2]); yticks([0 1]); yticklabels({'OFF', 'ON'}); ylabel('Command State');
ax = gca; ax.YAxis(1).Color = 'k';

yyaxis right;
hP = stairs(t, P_heater, 'm-', 'LineWidth', 1.6); 
ylabel('Heater Power [W]'); ylim([-100 3000]);
ax.YAxis(2).Color = [0.7 0 0.7];

xline(t_active, 'k:', 'Idle \rightarrow Active', 'LineWidth', 1.2, 'FontWeight', 'bold', ...
      'LabelHorizontalAlignment', 'right', 'LabelVerticalAlignment', 'top');
xline(t_loss, 'r--', 'COMM LOST', 'LineWidth', 1.6, ...
      'LabelHorizontalAlignment', 'left', 'LabelVerticalAlignment', 'top');
xlim([0 t_end]);
title('Heater Demand vs Safety Interlock — heating blocked on comm loss (fail-safe)');
legend([hD hS hP], {'User/System Demand', 'Safety-approved Heating', 'Actual Heater Power'}, 'Location', 'east');

% FIXED: Switch back to left axis context so 0.85 maps to the 0-1 scale correctly
yyaxis left;
text(t_loss + 2.0, 0.25, 'Demand ON — heating BLOCKED', 'Color', [0.7 0 0], 'FontWeight', 'bold');

% ================= Subplot 3: Tank Temperature Response =================
subplot(3, 1, 3); hold on; grid on;
yl = [min(T) - 0.01, max(T) + 0.02];
T_shade = yl(1) + double(t >= t_loss) * (yl(2) - yl(1));
area(t, T_shade, 'BaseValue', yl(1), 'FaceColor', [1 0.6 0.6], 'FaceAlpha', 0.18, ...
     'EdgeColor', 'none', 'HandleVisibility', 'off');
plot(t, T, 'r-', 'LineWidth', 2);
xline(t_active, 'k:', 'LineWidth', 1.2); 
xline(t_loss, 'r--', 'LineWidth', 1.6);
ylim(yl); xlim([0 t_end]); ylabel('Tank Temp [\circC]'); xlabel('Time [s]');
title('Tank Temperature — ramps while active, plateaus after comm loss (no runaway)');
text(t_loss + 2.0, max(T) - 0.005, 'Heating stopped', 'Color', [0.7 0 0], 'FontWeight', 'bold', 'VerticalAlignment', 'top');

% Save output plot
if ~exist('figures', 'dir'), mkdir figures; end
saveas(gcf, fullfile('figures', 'Scenario_6_PLC_CommLoss.png'));

% --- Print Report to Console ---
fprintf('\n===== SCENARIO 6: PLC COMM LOSS =====\n');
fprintf('  0-%.0fs  : Idle (no demand)\n', t_active);
fprintf('  %.0f-%.0fs : Active heating (demand + comm OK -> 2500W)\n', t_active, t_loss);
fprintf('  %.0f-%.0fs : COMM LOST -> demand ON, safety blocks -> 0W, temp plateaus (FAIL-SAFE)\n', t_loss, t_end);
fprintf('  Tank rose only %.3f C in %ds (huge thermal mass -> trip is instant vs any risk)\n', max(T)-T(1), t_end);
fprintf('=====================================\n');