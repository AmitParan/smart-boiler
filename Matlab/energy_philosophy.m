function energy_philosophy()
% Real-life 24h comparison:
%   1. Dumb Wasteful  (ON early, 65C, OFF after)   -> comfort but expensive + overheats
%   2. Dumb Cold      (ON only 1h before, 65C)     -> cheap but cold shower
%   3. Smart Hybrid   (ON precisely to 40C + boost)-> comfort AND cheap
% Saves: figures/energy_philosophy.png, energy_breakdown.png, energy_stacked_breakdown.png
    close all; clc;
    if exist('boiler_params.m','file')
        boiler_params; 
        P = pack_boiler_params();
    else
        % Standalone fallback parameters
        P.Water_Mass_kg=150; P.Cp_Water=4184; P.UA_Losses=1.5;
        P.P_Internal_W=2500; P.P_Boost_W=3000; P.T_Ambient_C=20;
        P.T_Water_Inlet_C=20; P.T_Comfort_C=38; P.h_Boost=3000; P.A_Boost=0.011;
    end
    mC=P.Water_Mass_kg*P.Cp_Water; Cp=P.Cp_Water; UA=P.UA_Losses;
    Ta=P.T_Ambient_C; Tin=P.T_Water_Inlet_C; Pel=P.P_Internal_W; Pb=P.P_Boost_W; Tc=P.T_Comfort_C;
    t_end=24*3600; dt=10; t=(0:dt:t_end)'; n=numel(t);
    showers=[7 19]*3600; dur=6*60; FLOW=8;                     % 2 showers, 6 min @ 8 LPM
    flow=zeros(n,1); for s=showers, flow(t>=s & t<s+dur)=FLOW; end
    
    % Turn-on leads from heat-up physics
    heat_s   = @(Tset) mC*(Tset-Tin)/Pel;                      % seconds to heat whole tank Tin->Tset
    smart_lead = heat_s(40);                                   % precise: reaches 40C exactly at shower
    user_margin = 30*60;                                       % dumb user switches on 30 min "just in case"
    dumb_lead  = heat_s(65) + user_margin;                     % imprecise + slow -> big lead
    cold_lead  = 60*60;                                        % under-heated user switches on only 1h before
    setD=nan(n,1); setC=nan(n,1); setS=nan(n,1);               % NaN = heater OFF
    for s=showers
        setD(t>=s-dumb_lead  & t<s+dur)=65;
        setC(t>=s-cold_lead  & t<s+dur)=65;
        setS(t>=s-smart_lead & t<s+dur)=40;
    end
    [TD,ED,DdD,BD]=sim_tank(t,flow,mC,Cp,UA,Ta,Tin,Pel,Pb,setD,false,Tc);  % dumb wasteful
    [TC,EC,DdC,BC]=sim_tank(t,flow,mC,Cp,UA,Ta,Tin,Pel,Pb,setC,false,Tc);  % dumb cold
    [TS,ES,DdS,BS]=sim_tank(t,flow,mC,Cp,UA,Ta,Tin,Pel,Pb,setS,true ,Tc);  % smart hybrid
    th=t/3600; tot=[ED(end) EC(end) ES(end)]; Dm=[DdD DdC DdS]; Bmat=[BD;BC;BS];
    
    % Clean names for graphics and tables
    names={'Dumb (Wasteful, 65°C)','Dumb (Cold, 1h manual)','Smart Hybrid (Our System)'};
    shrt={'Dumb (Wasteful)','Dumb (Cold 1h)','Smart Hybrid'};
    sav = (1-tot(3)/tot(1))*100;
    if ~exist('figures','dir'), mkdir figures; end
    
    % ============ Figure 1: Temperature + Cumulative Energy ============
    f1=figure('Color','w','Position',[70 40 970 740]);
    subplot(2,1,1); hold on; grid on;
    h1=plot(th,TD,'r-','LineWidth',1.7);
    h2=plot(th,TC,'Color',[0.9 0.5 0],'LineStyle','--','LineWidth',1.6);
    h3=plot(th,TS,'g-','LineWidth',1.8);
    yline(Tc,'k:','Comfort 38°C','HandleVisibility','off');
    hSh=[];
    for s=showers
        hs=xline(s/3600,'c-','LineWidth',1.2);
        if isempty(hSh), hSh=hs; else, set(hs,'HandleVisibility','off'); end
        
        xD=(s-dumb_lead)/3600; xS=(s-smart_lead)/3600;
        plot([xD xD],[15 75],'r:','LineWidth',1.1,'HandleVisibility','off');
        plot([xS xS],[15 75],'g:','LineWidth',1.1,'HandleVisibility','off');
        
        % Placed text inside axis range 
        text(xD, 62, sprintf('Dumb ON %s',hhmm(s-dumb_lead)), 'Color', 'r', ...
             'Rotation', 90, 'FontSize', 8, 'VerticalAlignment', 'top', 'FontWeight', 'bold');
        text(xS, 62, sprintf('Smart ON %s',hhmm(s-smart_lead)), 'Color', [0 .5 0], ...
             'Rotation', 90, 'FontSize', 8, 'VerticalAlignment', 'top', 'FontWeight', 'bold');
    end
    ylabel('Tank Temp [°C]'); xlim([0 24]); xticks(0:2:24); ylim([15 75]);
    title('Reservoir Temperature — Real-Life Manual vs. Smart Predictive');
    legend([h1 h2 h3 hSh],[names {'Shower active'}],'Location','best');
    
    subplot(2,1,2); hold on; grid on;
    plot(th,ED,'r-','LineWidth',1.9);
    plot(th,EC,'Color',[0.9 0.5 0],'LineStyle','--','LineWidth',1.7);
    plot(th,ES,'g-','LineWidth',2.0);
    for s=showers, xline(s/3600,'c-','LineWidth',1.2,'HandleVisibility','off'); end
    ylabel('Cumulative Energy [kWh]'); xlabel('Time of day [h]'); xlim([0 24]); xticks(0:2:24);
    title('Cumulative Electrical Energy — Real-life event-driven usage');
    legend(sprintf('%s: %.2f kWh',names{1},tot(1)), ...
           sprintf('%s: %.2f kWh',names{2},tot(2)), ...
           sprintf('%s: %.2f kWh',names{3},tot(3)),'Location','northwest');
    save_fig(f1,'figures/energy_philosophy.png');
    
    % ============ Figure 2: KPI Table Summary ============
    comf=arrayfun(@(x) ternary(x>=Tc,'YES','NO'), Dm,'uni',0);
    C={ names{1}, sprintf('%.2f',tot(1)), sprintf('%.0f',dumb_lead/60),  sprintf('%.1f',DdD), comf{1}, '0.0 (ref)';
        names{2}, sprintf('%.2f',tot(2)), sprintf('%.0f',cold_lead/60),  sprintf('%.1f',DdC), comf{2}, sprintf('%.1f',(1-tot(2)/tot(1))*100);
        names{3}, sprintf('%.2f',tot(3)), sprintf('%.0f',smart_lead/60), sprintf('%.1f',DdS), comf{3}, sprintf('%.1f',sav) };
    cols={'Strategy','24h Energy [kWh]','Turn-on lead [min]','Min Delivered [°C]','Comfort >=38°C','Saving vs Conv [%]'};
    f2=figure('Color','w','Position',[70 220 1080 190]);
    uitable(f2,'Data',C,'ColumnName',cols,'RowName',[],'Units','normalized', ...
        'Position',[0.01 0.01 0.98 0.98],'FontSize',11);
    save_fig(f2,'figures/energy_breakdown.png');
    
    % ============ Figure 3: Exact 4-way Stacked Breakdown ============
    f3=figure('Color','w','Position',[70 120 900 560]);
    b=bar(Bmat,'stacked','BarWidth',0.55); hold on;
    b(1).FaceColor=[.85 .35 .35];   % standing losses (red)
    b(2).FaceColor=[.30 .65 .35];   % useful delivered @38C (green)
    b(3).FaceColor=[.95 .65 .20];   % over-heat waste (>38C) (orange)
    b(4).FaceColor=[.35 .50 .80];   % stored / residual (blue)
    set(gca,'XTick',1:3,'XTickLabel',shrt,'FontSize',11); grid on;
    ylabel('Energy [kWh]'); ylim([0 max(tot)*1.18]);
    title(sprintf('Where the 24h Energy Goes — Smart saves %.0f%% vs Wasteful', sav));
    legend({'Standing losses (waste)','Useful delivered @38°C (comfort)', ...
            'Over-heat waste (>38°C)','Stored / residual'},'Location','northeast');
    for i=1:3
        text(i, tot(i)+max(tot)*0.02, sprintf('%.2f kWh',tot(i)), ...
             'HorizontalAlignment','center','FontWeight','bold');
    end
    save_fig(f3,'figures/energy_stacked_breakdown.png');
    
    % ============ Console Output ============
    fprintf('\n=================== REAL-LIFE 24h ENERGY ===================\n');
    fprintf('  %-22s %8s %8s %8s %8s %8s | Comfort\n','Strategy','Total','Loss','Useful','OverHt','Stored');
    for i=1:3
        B=Bmat(i,:);
        fprintf('  %-22s %7.2f  %6.2f  %6.2f  %6.2f  %6.2f | %s (min %.1fC)\n', ...
                shrt{i}, tot(i), B(1),B(2),B(3),B(4), comf{i}, Dm(i));
    end
    fprintf('  Dumb ON lead = %.0f min | Smart ON lead = %.0f min\n', dumb_lead/60, smart_lead/60);
    fprintf('  SMART HYBRID SAVES %.1f%% vs. Wasteful Conventional while maintaining comfort.\n', sav);
    fprintf('============================================================\n');
end

function [T,E,Dmin,brk]=sim_tank(t,flow,mC,Cp,UA,Ta,Tin,Pel,Pb,setpt,useBoost,Tc)
    n=numel(t); T=zeros(n,1); T(1)=Ta; Ph=zeros(n,1); Pbo=zeros(n,1); Del=zeros(n,1); on=false;
    loss_J=0; use_J=0; over_J=0;
    for k=1:n-1
        dtk=t(k+1)-t(k); sp=setpt(k);
        if ~isnan(sp)
            if T(k)<sp-2, on=true; elseif T(k)>sp, on=false; end
        else, on=false; end
        Ph(k)=on*Pel;
        mdot=flow(k)/60;
        if useBoost && flow(k)>0.1 && T(k)<Tc+1
            Pbo(k)=Pb; Del(k)=T(k)+Pb/(mdot*Cp);
        else
            Del(k)=T(k);
        end
        if flow(k)>0.1
            use_J  = use_J  + mdot*Cp*(min(Del(k),Tc)-Tin)*dtk;
            over_J = over_J + mdot*Cp*max(Del(k)-Tc,0)*dtk;
        end
        Qloss=UA*(T(k)-Ta); Qdraw=mdot*Cp*(T(k)-Tin);
        loss_J = loss_J + Qloss*dtk;
        T(k+1)=T(k)+(Ph(k)-Qloss-Qdraw)/mC*dtk;
    end
    Ph(end)=Ph(max(1,end-1)); Del(end)=Del(max(1,end-1));
    E=cumtrapz(t,Ph+Pbo)/3.6e6;
    sh=flow>0.1; if any(sh), Dmin=min(Del(sh)); else, Dmin=NaN; end
    stored_J=mC*(T(end)-T(1));
    brk=[loss_J, use_J, over_J, stored_J]/3.6e6;
end

function o=ternary(c,a,b), if c, o=a; else, o=b; end, end

function str=hhmm(sec)
    sec=mod(sec,86400); 
    str=sprintf('%02d:%02d',floor(sec/3600),floor(mod(sec,3600)/60)); 
end

function save_fig(f,p)
    try
        if ~isempty(findall(f, 'Type', 'uitable'))
            frame = getframe(f);
            imwrite(frame.cdata, p);
        else
            exportgraphics(f, p, 'Resolution', 180);
        end
    catch
        try
            print(f, p, '-dpng', '-r170');
        catch
            warning('Could not save figure to %s', p);
        end
    end
end