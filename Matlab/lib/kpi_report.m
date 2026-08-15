function K = kpi_report(S, cfg, P)
% Compute + print KPIs; publish LAST_KPI. Sheath temp stays console-only (proves safety).
    t = S.t; K.name = cfg.name; K.dur = t(end);
    K.E_smart_kWh = S.E_kWh(end);
    K.finalT = S.T_Internal(end); K.peakT = max(S.T_Internal);
    K.t_to_target = NaN;
    if isfield(cfg,'target') && ~isempty(cfg.target)
        i = find(S.T_Internal >= cfg.target,1); if ~isempty(i), K.t_to_target = t(i); end
    end
    if any(S.flow>0)
        mdot = S.flow/60/1000 * P.Rho_Water;
        useful = mdot .* P.Cp_Water .* max(0, S.T_deliver - P.T_Water_Inlet_C);
        K.E_useful_kWh   = trapz(t, useful)/3.6e6;
        K.liters_comfort = trapz(t, (S.flow/60).*(S.T_deliver>=P.T_Comfort_C));
    else
        K.E_useful_kWh = NaN; K.liters_comfort = 0;
    end
    K.eta = K.E_useful_kWh / max(K.E_smart_kWh, eps);
    K.E_dumb_kWh = NaN; K.saved_pct = NaN;
    if isfield(cfg,'P_dumb') && ~isempty(cfg.P_dumb)
        K.E_dumb_kWh = trapz(t,cfg.P_dumb)/3.6e6;
        if K.E_dumb_kWh > 1e-6, K.saved_pct = (1 - K.E_smart_kWh/K.E_dumb_kWh)*100; end
    end
    K.event=''; K.latency_ms=NaN;
    K.dT_boost_max = NaN; K.sheath_peak_C = NaN;
    if any(S.flow>0.1)
        fa = S.flow>0.1;
        K.dT_boost_max = max(S.T_deliver(fa) - S.T_Internal(fa));
        if isfield(S,'T_element'), K.sheath_peak_C = max(S.T_element(fa)); end
    end

    fprintf('\n============ KPI: %s ============\n', cfg.name);
    fprintf('  Duration          : %.0f s\n', t(end));
    fprintf('  Final / Peak temp : %.2f / %.2f C\n', K.finalT, K.peakT);
    if ~isnan(K.t_to_target), fprintf('  Time to %.0fC       : %.0f s\n', cfg.target, K.t_to_target); end
    fprintf('  Smart energy      : %.3f kWh\n', K.E_smart_kWh);
    if ~isnan(K.E_dumb_kWh)
        fprintf('  Dumb  energy      : %.3f kWh\n', K.E_dumb_kWh);
        if ~isnan(K.saved_pct), fprintf('  ENERGY SAVED      : %.1f %%\n', K.saved_pct); end
    end
    if ~isnan(K.E_useful_kWh)
        fprintf('  Useful delivered  : %.3f kWh  (eta = %.2f)\n', K.E_useful_kWh, K.eta);
        fprintf('  Comfort liters>=38: %.1f L\n', K.liters_comfort);
    end
    if ~isnan(K.dT_boost_max), fprintf('  Boost lift dT     : %.1f C (max)\n', K.dT_boost_max); end
    if ~isnan(K.sheath_peak_C)
        lim = 250; if isfield(P,'T_Sheath_Limit_C'), lim=P.T_Sheath_Limit_C; end
        fprintf('  Booster sheath pk : %.0f C (limit %.0f C) -> SAFE=%s\n', ...
                K.sheath_peak_C, lim, string(K.sheath_peak_C < lim));
    end
    fprintf('=================================================\n');
    assignin('base','LAST_KPI',K);
end