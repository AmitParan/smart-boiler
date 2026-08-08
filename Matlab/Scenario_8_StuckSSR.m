function Scenario_8_StuckSSR()
% Scenario 8 — Stuck SSR caught by the Current Sensor (ACS758) — immediate 50 ms lockout.
% Demonstrates active electrical diagnostic layer vs. slow thermal runaway cutoff.
% Saves: figures/Scenario_8_StuckSSR.png
    clear; clc; close all;
    
    if exist('boiler_params.m','file')
        boiler_params; 
        P = pack_boiler_params();
    else
        % Fallback parameters
        P.Water_Mass_kg = 150; P.Cp_Water = 4184; P.UA_Losses = 1.5;
        P.P_Internal_W  = 2500; P.T_Ambient_C = 20; P.T_Initial_C = 20;
    end
    
    t_end = 0.5; 
    dt = 0.001; 
    t = (0:dt:t_end)';
    
    t_cmd  = 0.2;            % Time [s]: Software issues OFF command
    t_trip = t_cmd + 0.05;   % Time [s]: Current-monitor lockout trips exactly 50 ms later
    
    % Forced to 13.6A to match the exact system specification for uncommanded current fault
    I_fault = 13.6;          
    Pn = P.P_Internal_W; 
    
    % --- Control & Electrical Signals ---
    cmd_soft  = double(t < t_cmd);              % Software control state: ON until 0.2s, then OFF
    ssr_state = ones(size(t));                  % SSR is STUCK closed (always physical ON)
    powered   = double(t < t_trip);             % Safety contactor physically cuts power at 0.05s trip
    current   = I_fault * powered;              % Current read by ACS758 sensor
    fault     = double(t >= t_trip);            % Fault state latched after trip
    mismatch  = (cmd_soft < 0.5) & (current > 1); % Mismatch zone
    
    % --- High-Res Temperature Simulation ---
    mC = P.Water_Mass_kg * P.Cp_Water; 
    T = zeros(size(t)); 
    T(1) = 70;                                  % Starting temperature representing hot tank operation
    for k = 1:numel(t)-1
        Qloss = P.UA_Losses * (T(k) - P.T_Ambient_C);
        T(k+1) = T(k) + (powered(k)*Pn - Qloss)/mC * (t(k+1) - t(k));
    end
    
    % Create Consolidated Analysis Figure
    f1 = figure('Name', 'Scenario 8: Stuck SSR — Current-Sensor Lockout', 'Color', 'w', 'Position', [70 50 940 800]);
    
    % ---- Subplot 1: Command vs Stuck SSR vs Fault ----
    subplot(3,1,1); hold on; grid on;
    hC = stairs(t, cmd_soft, 'b-', 'LineWidth', 2.2);
    hS = stairs(t, ssr_state, 'g--', 'LineWidth', 1.8);
    hF = stairs(t, fault, 'r-', 'LineWidth', 1.6);
    xline(t_cmd, 'k:', 'SW OFF', 'LineWidth', 1.2, 'FontWeight', 'bold', 'LabelHorizontalAlignment', 'right', 'LabelVerticalAlignment', 'top');
    xline(t_trip, 'r--', 'LOCKOUT', 'LineWidth', 1.5, 'FontWeight', 'bold', 'LabelHorizontalAlignment', 'left', 'LabelVerticalAlignment', 'top');
    ylim([-0.1 1.2]); yticks([0 1]); yticklabels({'OFF', 'ON'}); ylabel('State'); xlim([0 t_end]);
    title('Software Command vs Stuck SSR vs Fault Lockout');
    legend([hC hS hF], {'Software Command', 'Actual SSR (STUCK ON)', 'Fault Lockout'}, 'Location', 'east', 'FontSize', 8);
    
    % ---- Subplot 2: ACS758 current (The Detection Signal) ----
    subplot(3,1,2); hold on; grid on;
    area(t, double(mismatch)*I_fault*1.25, 'BaseValue', 0, 'FaceColor', [1 0.6 0.6], 'FaceAlpha', 0.30, ...
         'EdgeColor', 'none', 'HandleVisibility', 'off');
    stairs(t, current, 'm-', 'LineWidth', 2.4);
    yline(1, 'k:', 'detect threshold', 'LineWidth', 1);
    xline(t_cmd, 'k:', 'LineWidth', 1.2); 
    xline(t_trip, 'r--', 'LineWidth', 1.5);
    ylim([-0.5 I_fault*1.35]); ylabel('Current [A] (ACS758)'); xlim([0 t_end]);
    title(sprintf('Current Sensor: %.1f A flowing despite OFF \\rightarrow trips in 50 ms', I_fault));
    text(t_cmd + 0.004, I_fault*0.5, 'Current detected while commanded OFF', 'Color', [0.7 0 0], 'FontWeight', 'bold');
    legend('Measured current', 'Location', 'east', 'FontSize', 8);
    
    % ---- Subplot 3: Temperature (Instant Reaction) ----
    subplot(3,1,3); hold on; grid on;
    plot(t, T, 'b-', 'LineWidth', 2.2);
    yline(80, 'r--', 'Software 80\circC Limit', 'LineWidth', 1.2, 'LabelHorizontalAlignment', 'right'); 
    yline(85, 'k--', 'Hardware 85\circC Limit', 'LineWidth', 1.2, 'LabelHorizontalAlignment', 'right');
    xline(t_trip, 'r--', 'LineWidth', 1.5);
    ylim([60 90]); xlim([0 t_end]); ylabel('Tank Temp [\circC]'); xlabel('Time [s]');
    title('Tank Temperature — Electrical Timeframe Transient (0.5s)');
    
    % FIXED: Highly explicit descriptions clarifying the flat temperature behavior
    text(0.015, 73, 'Electrical disconnect occurs in 50 ms \rightarrow Temp cannot rise (\DeltaT = 0.000\circC)', 'Color', [0 0.45 0], 'FontWeight', 'bold');
    text(0.015, 65, '*Note: System isolates the failure instantly, long before thermal runaway conditions develop.', 'Color', [0.4 0.4 0.4], 'FontSize', 9);
    
    % Save output plot
    if ~exist('figures', 'dir'), mkdir figures; end
    save_fig(f1, 'figures/Scenario_8_StuckSSR.png');
    
    % ===== Console Output Report =====
    fprintf('\n===== SCENARIO 8: STUCK SSR (CURRENT-SENSOR LOCKOUT) =====\n');
    fprintf('  Software OFF @ %.3fs | current still %.1f A (stuck SSR)\n', t_cmd, I_fault);
    fprintf('  ACS758 mismatch -> Fault lockout @ %.3fs (latency %.0f ms)\n', t_trip, (t_trip - t_cmd)*1000);
    fprintf('  Tank rose only %.4f C -> caught ~%.1f C below the 85C thermal cutoff.\n', max(T)-70, 85-max(T));
    fprintf('  Contrast 7B (acts only at 85C thermal): here we act ELECTRICALLY in 50 ms.\n');
    fprintf('==========================================================\n');
end

function save_fig(f, p)
    try
        exportgraphics(f, p, 'Resolution', 170);
    catch
        print(f, p, '-dpng', '-r170');
    end
end