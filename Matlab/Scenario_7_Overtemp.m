function Scenario_7_Overtemp()
% Scenario 7 — Thermal Runaway & Redundant Safety (2 cases, script-emulated, accelerated view).
%   7A: software cutoff @80C succeeds -> safe plateau.
%   7B: software fails (stuck SSR) -> hardware cutoff @85C physically cuts power.

    close all; clc;
    
    if exist('boiler_params.m', 'file')
        boiler_params; 
        P = pack_boiler_params();
    else
        % Standalone fallback parameters
        P.Water_Mass_kg = 150; P.Cp_Water = 4184; P.UA_Losses = 1.5;
        P.P_Internal_W  = 2500; P.T_Ambient_C = 20; P.T_Initial_C = 20;
    end

    Tsw  = 80;    % Software Over-temperature threshold [C]
    Thw  = 85;    % Hardware Cutoff thermal switch threshold [C]
    T0   = 70;    % Start temperature for the accelerated run [C]
    Pn   = P.P_Internal_W;
    ramp = 0.4;   % Accelerated runaway heating rate [C/s] (for demo clarity)
    
    t_end = 60; 
    dt = 0.05;
    mC = P.Water_Mass_kg * P.Cp_Water; 
    UA = P.UA_Losses; 
    Ta = P.T_Ambient_C;

    % Run Case A (Software control works normally)
    [tA, TA, cmdA, ssrA, pwA] = run_case(false, ramp, T0, t_end, dt, Tsw, Thw, Pn, mC, UA, Ta);
    
    % Run Case B (Software fails / SSR stuck closed -> Hardware backup saves the system)
    [tB, TB, cmdB, ssrB, pwB] = run_case(true, ramp, T0, t_end, dt, Tsw, Thw, Pn, mC, UA, Ta);

    % Create Consolidated Safety Comparison Figure
    figure('Name', 'Scenario 7: Thermal Runaway & Redundant Safety', 'Color', 'w', 'Position', [60 40 1120 720]);

    % ===== Top-left: Case 7A temperature =====
    subplot(2,2,1); hold on; grid on;
    plot(tA, TA, 'b-', 'LineWidth', 2.2);
    yline(Tsw, 'r--', 'Software 80\circC', 'LineWidth', 1.4);
    yline(Thw, 'k--', 'Hardware 85\circC', 'LineWidth', 1.4);
    
    kA = find(TA >= Tsw, 1); 
    if ~isempty(kA)
        plot(tA(kA), TA(kA), 'ro', 'MarkerFaceColor', 'r'); 
        xline(tA(kA), 'r:', 'HandleVisibility', 'off'); 
        text(tA(kA) + 1, 82, 'SW OFF @80\circC \rightarrow safe plateau', 'Color', [0.7 0 0], 'FontWeight', 'bold');
    end
    ylim([60 100]); xlim([0 t_end]); ylabel('Tank Temp [\circC]');
    title('Case 7A: Software Cutoff (Success)');

    % ===== Top-right: Case 7B temperature =====
    subplot(2,2,2); hold on; grid on;
    plot(tB, TB, 'b-', 'LineWidth', 2.2);
    yline(Tsw, 'r--', 'Software 80\circC', 'LineWidth', 1.4);
    yline(Thw, 'k--', 'Hardware 85\circC', 'LineWidth', 1.4);
    
    kBsw = find(TB >= Tsw, 1); 
    kBhw = find(TB >= Thw, 1);
    if ~isempty(kBsw)
        plot(tB(kBsw), TB(kBsw), 'o', 'Color', [0.6 0.6 0.6], 'MarkerFaceColor', [0.6 0.6 0.6]); 
    end
    if ~isempty(kBhw)
        plot(tB(kBhw), TB(kBhw), 'ks', 'MarkerFaceColor', 'k'); 
        xline(tB(kBhw), 'k:', 'HandleVisibility', 'off'); 
    end
    ylim([60 100]); xlim([0 t_end]); ylabel('Tank Temp [\circC]');
    title('Case 7B: Hardware Backup (Software Failed / Stuck SSR)');
    text(2, 97, 'SW failed at 80\circC \rightarrow HW cut @85\circC', 'Color', [0 0 0], 'FontWeight', 'bold');

    % ===== Bottom-left: Case 7A SSR control =====
    plot_ssr(subplot(2,2,3), tA, cmdA, ssrA, pwA, '7A: SSR Control (software de-energizes SSR)', t_end);
    
    % ===== Bottom-right: Case 7B SSR control =====
    plot_ssr(subplot(2,2,4), tB, cmdB, ssrB, pwB, '7B: SSR Control (SSR stuck ON \rightarrow HW cuts power)', t_end);

    % Save high-res plot
    if ~exist('figures', 'dir'), mkdir figures; end
    save_fig(gcf, 'figures/Scenario_7_Overtemp.png');

    % ===== Console Output =====
    fprintf('\n===== SCENARIO 7: REDUNDANT SAFETY =====\n');
    if ~isempty(kA)
        fprintf('  7A: software cutoff at %.1f s (T=%.1fC) -> peak %.1fC (SAFE, below 85)\n', tA(kA), TA(kA), max(TA));
    end
    if ~isempty(kBhw)
        fprintf('  7B: software commanded OFF at 80C but SSR stuck; HW cut at %.1f s (T=%.1fC) -> peak %.1fC\n', ...
                tB(kBhw), TB(kBhw), max(TB));
    end
    fprintf('  (Accelerated view: real 150L @2500W would cross these over hours; safety logic is temp-based, not time-based.)\n');
    fprintf('========================================\n');
end

% ---------- local functions ----------

function [t, T, cmd_soft, ssr, power] = run_case(stuck, ramp, T0, t_end, dt, Tsw, Thw, Pn, mC, UA, Ta)
    t = (0:dt:t_end)'; 
    n = numel(t);
    T = zeros(n,1); 
    T(1) = T0; 
    cmd_soft = zeros(n,1); 
    ssr = zeros(n,1); 
    power = zeros(n,1);
    
    for k = 1:n
        cmd_soft(k) = double(T(k) < Tsw);                           % software: ON below 80, OFF at/above 80
        if stuck
            ssr_on = true; 
        else
            ssr_on = cmd_soft(k) > 0.5; 
        end                                                         % stuck SSR ignores software command
        
        hard_ok = T(k) < Thw;                                       % hardware thermal cutoff at 85
        heating = ssr_on && hard_ok;
        
        ssr(k)   = double(ssr_on); 
        power(k) = heating * Pn;
        
        if k < n
            dTdt = heating * ramp - UA * (T(k) - Ta)/mC;            % accelerated heating + real thermal losses
            T(k+1) = T(k) + dTdt * (t(k+1) - t(k));
        end
    end
end

function plot_ssr(ax, t, cmd, ssr, power, ttl, t_end)
    axes(ax); hold on; grid on;
    yyaxis left;
    hC = stairs(t, cmd, 'b-', 'LineWidth', 2.0);
    hS = stairs(t, ssr, 'g--', 'LineWidth', 1.8);
    ylim([-0.1 1.2]); yticks([0 1]); yticklabels({'OFF', 'ON'}); ylabel('Command'); ax.YAxis(1).Color = 'k';
    
    yyaxis right;
    hP = stairs(t, power, 'm-', 'LineWidth', 1.5); 
    ylabel('Heater Power [W]'); ylim([-100 3000]); 
    ax.YAxis(2).Color = [0.7 0 0.7];
    
    xlabel('Time [s]'); xlim([0 t_end]); title(ttl);
    legend([hC hS hP], {'Software Command', 'Actual SSR State', 'Heater Power'}, 'Location', 'east', 'FontSize', 8);
end

function o = ternary(c,a,b), if c, o=a; else, o=b; end, end

function save_fig(f, p)
    try
        if ~isempty(findall(f, 'Type', 'uitable'))
            frame = getframe(f);
            imwrite(frame.cdata, p);
        else
            exportgraphics(f, p, 'Resolution', 170);
        end
    catch
        try
            print(f, p, '-dpng', '-r170');
        catch
            warning('Could not save figure to %s', p);
        end
    end
end