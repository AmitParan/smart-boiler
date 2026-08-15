function [T, P_heat] = dumb_baseline(t, P, flow_LPM, mode)
% Reference conventional boiler.
%   mode='thermostat' (default): maintains 65C (2C hysteresis) + optional draw.
%   mode='passive'   : heater OFF (nobody switched it on) -> delivers cold water.
    t=t(:); n=numel(t);
    if nargin<3||isempty(flow_LPM), flow_LPM=zeros(n,1); end
    if nargin<4||isempty(mode),     mode='thermostat'; end
    flow_LPM=flow_LPM(:);
    T=zeros(n,1); T(1)=P.T_Initial_C; P_heat=zeros(n,1); on=true;
    mC=P.Water_Mass_kg*P.Cp_Water; passive=strcmpi(mode,'passive');
    for k=1:n-1
        if passive, on=false;
        elseif T(k)>=P.T_dumb_target,      on=false;
        elseif T(k)<=P.T_dumb_target-2,    on=true; end
        P_heat(k)=on*P.P_Internal_W;
        Qloss=P.UA_Losses*(T(k)-P.T_Ambient_C);
        mdot =flow_LPM(k)/60/1000*P.Rho_Water;
        Qdraw=mdot*P.Cp_Water*(T(k)-P.T_Water_Inlet_C);
        T(k+1)=T(k)+(P_heat(k)-Qloss-Qdraw)/mC*(t(k+1)-t(k));
    end
    P_heat(end)=P_heat(max(1,end-1));
end