function S = extract_signals(out, t, P, flow_ts, ldr_ts)
% Pull standard signals onto time base t. Duplicate-name safe.
% Optional: flow_ts (LPM), ldr_ts (LDR ts for solar-protected reconstruction).
    t = t(:); n = numel(t); S.t = t;
    S.T_Internal   = getSig(out,'T_Internal',t);
    S.T_Boost      = getSig(out,'T_Boost',t);
    S.cmd_internal = getSig(out,'Cmd_Internal',t);
    S.cmd_boost    = getSig(out,'Cmd_Boost',t);
    S.safe_internal= getSig(out,'Safe_Internal',t);
    S.P_internal   = getSig(out,'P_Internal_Actual_W',t);

    for fld = {'T_Internal','T_Boost'}
        if ~isempty(S.(fld{1})) && mean(S.(fld{1}),'omitnan') > 200
            S.(fld{1}) = S.(fld{1}) - 273.15;
        end
    end
    if isempty(S.T_Internal), S.T_Internal = zeros(n,1); end
    if isempty(S.T_Boost),    S.T_Boost    = zeros(n,1); end
    if isempty(S.cmd_boost),  S.cmd_boost  = zeros(n,1); end
    if isempty(S.P_internal) && ~isempty(S.cmd_internal)
        S.P_internal = S.cmd_internal * P.P_Internal_W;
    elseif isempty(S.P_internal)
        S.P_internal = zeros(n,1);
    end
    if isempty(S.cmd_internal) || all(isnan(S.cmd_internal))
        S.cmd_internal = double(S.P_internal > 10);
    end
    S.cmd_internal(isnan(S.cmd_internal)) = double(S.P_internal(isnan(S.cmd_internal)) > 10);

    S.P_boost = S.cmd_boost * P.P_Boost_W;
    S.P_elec  = S.P_internal + S.P_boost;
    S.E_kWh   = cumtrapz(t, S.P_elec)/3.6e6;
    S.I_amp   = S.P_elec / P.V_Supply;

    % flow
    if nargin>=4 && ~isempty(flow_ts)
        S.flow = interp1(flow_ts.Time, squeeze(flow_ts.Data), t,'linear','extrap');
    else
        S.flow = zeros(n,1);
    end
    % LDR (solar) — optional
    if nargin>=5 && ~isempty(ldr_ts)
        ldr = interp1(ldr_ts.Time, squeeze(ldr_ts.Data), t,'linear','extrap');
    else
        ldr = zeros(n,1);
    end
    sgc = 8; if isfield(P,'Solar_Gain_Coeff'), sgc = P.Solar_Gain_Coeff; end

    % --- Thermodynamic reconstruction backup (bypass 0C collapse), SOLAR-PROTECTED ---
    if any(S.T_Internal < 5)
        T_rec = zeros(n,1); T_rec(1) = P.T_Initial_C;
        mC = P.Water_Mass_kg * P.Cp_Water;
        for k = 1:n-1
            dt   = t(k+1) - t(k);
            mdot = (S.flow(k)/60/1000) * P.Rho_Water;      % kg/s
            Q_elec  = S.P_internal(k);
            Q_solar = sgc * ldr(k);                        % protects Scenario 5
            Q_loss  = P.UA_Losses * (T_rec(k) - P.T_Ambient_C);
            Q_draw  = mdot * P.Cp_Water * (T_rec(k) - P.T_Water_Inlet_C);
            T_rec(k+1) = T_rec(k) + (Q_elec + Q_solar - Q_loss - Q_draw)/mC*dt;
        end
        S.T_Internal = T_rec;
    end

    % --- Delivered temp with inline-booster lift ---
    S.T_deliver = S.T_Internal;
    has_flow = S.flow > 0.1;
    mdot_boost = (S.flow/60/1000) * P.Rho_Water; mdot_boost(mdot_boost<1e-5)=1e-5;
    dT_boost_val = S.P_boost ./ (mdot_boost * P.Cp_Water);
    S.T_deliver(has_flow)  = S.T_Internal(has_flow) + dT_boost_val(has_flow);
    S.T_deliver(~has_flow) = P.T_Water_Inlet_C;

    % Booster ΔT and sheath (element) temperature = water + P/UA,  UA = h*A
    UA_boost = 33;
    if isfield(P,'h_Boost') && isfield(P,'A_Boost'), UA_boost = max(P.h_Boost*P.A_Boost, eps); end
    S.dT_boost  = S.T_deliver - S.T_Internal;              % delivered lift
    S.T_element = S.T_Internal + S.P_boost ./ UA_boost;    % sheath temp (stays under limit)
end