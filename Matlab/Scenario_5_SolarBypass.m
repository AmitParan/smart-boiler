function Scenario_5_SolarBypass()
% Scenario 5 — Predictive Solar (Sunny & Cloudy), 3 systems, 19:00 shower @ 40C target.
%   Electric-only (manual 1h, no panel) | Conventional solar (manual 1h, blind) | Smart hybrid (forecast + boost)
    close all; clc;
    if exist('boiler_params.m','file')
        boiler_params; P=pack_boiler_params();
    else
        P.Water_Mass_kg=150;P.Cp_Water=4184;P.UA_Losses=1.5;P.P_Internal_W=2500; ...
        P.P_Boost_W=3000;P.T_Ambient_C=20;P.T_Water_Inlet_C=20;P.T_Comfort_C=38; 
    end
    t_end=24*3600; dt=10; t=(0:dt:t_end)'; n=numel(t);
    ss=19*3600; se=ss+6*60; FLOW=8;                          % shower 19:00, 6 min, 8 LPM
    flow=zeros(n,1); flow(t>=ss & t<se)=FLOW;
    sunrise=6*3600; sunset=18*3600;
    bell=@(pk) pk*max(0,sin(pi*(t-sunrise)/(sunset-sunrise))).*(t>=sunrise & t<=sunset);
    Qsun=bell(900); Qcld=bell(280);                          % solar thermal power [W] (tune peaks)
    LDRsun=1023*Qsun/900; LDRcld=1023*Qcld/900;
    Esun=trapz(t,Qsun)/3.6e6; Ecld=trapz(t,Qcld)/3.6e6;      % solar harvested [kWh]
    manual_lead=60*60;                                       % REALISTIC: user switches electric on ~1 h before
    Sun.E=sim_day(t,flow,[],   'blind', P,manual_lead,ss,se);
    Sun.C=sim_day(t,flow,Qsun, 'blind', P,manual_lead,ss,se);   % conventional solar = blind + solar
    Sun.S=sim_day(t,flow,Qsun, 'smart', P,manual_lead,ss,se);
    Cld.E=sim_day(t,flow,[],   'blind', P,manual_lead,ss,se);
    Cld.C=sim_day(t,flow,Qcld, 'blind', P,manual_lead,ss,se);
    Cld.S=sim_day(t,flow,Qcld, 'smart', P,manual_lead,ss,se);
    th=t/3600; Tc=P.T_Comfort_C; if ~exist('figures','dir'), mkdir figures; end
    % ===== Figure 1: 2x2 temps & energy =====
    f1=figure('Color','w','Position',[50 30 1180 760]);
    plot_temps (subplot(2,2,1),th,Sun,LDRsun,'SUNNY day  —  Tank Temperature', Tc,ss,Esun);
    plot_energy(subplot(2,2,2),th,Sun,'SUNNY day  —  Electric Energy');
    plot_temps (subplot(2,2,3),th,Cld,LDRcld,'CLOUDY day  —  Tank Temperature',Tc,ss,Ecld);
    plot_energy(subplot(2,2,4),th,Cld,'CLOUDY day  —  Electric Energy');
    save_fig(f1,'figures/Scenario_5_SolarBypass.png');
    % ===== Figure 2: grouped electric-energy bar =====
    Emat=[Sun.E.Eelec Sun.C.Eelec Sun.S.Eelec; Cld.E.Eelec Cld.C.Eelec Cld.S.Eelec];
    f2=figure('Color','w','Position',[80 120 820 520]);
    b=bar(Emat,'grouped','BarWidth',0.85); hold on; grid on;
    b(1).FaceColor=[.85 .3 .3]; b(2).FaceColor=[.9 .55 .1]; b(3).FaceColor=[.25 .6 .3];
    set(gca,'XTickLabel',{'Sunny day','Cloudy day'},'FontSize',11);
    ylabel('Electric Energy [kWh]'); title('Daily Electricity: Electric-only vs Conventional-solar vs Smart');
    legend({'Electric-only (manual 1h)','Conventional solar (blind 1h)','Smart hybrid (predictive)'},'Location','northwest');
    ymax=max(Emat(:));
    for i=1:size(Emat,1)
        for j=1:3
            text(b(j).XEndPoints(i), b(j).YEndPoints(i)+ymax*0.02, sprintf('%.2f',Emat(i,j)), ...
                 'HorizontalAlignment','center','FontSize',9,'FontWeight','bold'); 
        end
    end
    ylim([0 ymax*1.18]); save_fig(f2,'figures/Scenario_5_energy_bar.png');
    % ===== Figure 3: KPI table =====
    rows={'Sunny','Electric-only',Sun.E; 'Sunny','Conv solar',Sun.C; 'Sunny','Smart hybrid',Sun.S; ...
          'Cloudy','Electric-only',Cld.E;'Cloudy','Conv solar',Cld.C;'Cloudy','Smart hybrid',Cld.S};
    ref=[Sun.E.Eelec;Sun.E.Eelec;Sun.E.Eelec;Cld.E.Eelec;Cld.E.Eelec;Cld.E.Eelec];
    C=cell(6,6);
    for i=1:6
        R=rows{i,3};
        C(i,:)={rows{i,1}, rows{i,2}, sprintf('%.2f',R.Eelec), sprintf('%.1f',R.Dmin), ...
                ternary(R.Dmin>=Tc,'YES','NO'), sprintf('%.1f',(1-R.Eelec/ref(i))*100)}; 
    end
    cols={'Day','System','Electric [kWh]','Min Delivered [C]','Comfort >=38C','Saved vs Elec-only [%]'};
    f3=figure('Color','w','Position',[80 220 1000 260]);
    uitable(f3,'Data',C,'ColumnName',cols,'RowName',[],'Units','normalized', ...
        'Position',[0.01 0.01 0.98 0.98],'FontSize',11);
    save_fig(f3,'figures/Scenario_5_kpi_table.png');
    % ===== console =====
    fprintf('\n============== SCENARIO 5: PREDICTIVE SOLAR (real 1h manual) ==============\n');
    fprintf('  Solar harvested: Sunny %.2f kWh | Cloudy %.2f kWh (free)\n',Esun,Ecld);
    for i=1:6
        R=rows{i,3};
        fprintf('  %-7s %-14s %6.2f kWh | min %5.1fC | comfort %s\n', ...
                rows{i,1},rows{i,2},R.Eelec,R.Dmin,ternary(R.Dmin>=Tc,'YES','NO')); 
    end
    fprintf('==========================================================================\n');
end

function R=sim_day(t,flow,Q,mode,P,manual_lead,ss,se)
    n=numel(t); dt=t(2)-t(1);
    mC=P.Water_Mass_kg*P.Cp_Water; Cp=P.Cp_Water; UA=P.UA_Losses;
    Ta=P.T_Ambient_C; Tin=P.T_Water_Inlet_C; Pel=P.P_Internal_W; Pbo=P.P_Boost_W; Tc=P.T_Comfort_C;
    if isempty(Q), Q=zeros(n,1); end
    cumQ=cumtrapz(t,Q); solarByShower=interp1(t,cumQ,ss);
    T=zeros(n,1); T(1)=Ta; Pe=zeros(n,1); Pb=zeros(n,1); Del=zeros(n,1); on=false;
    for k=1:n-1
        switch mode
            case 'blind'                                    % realistic: heat full for ~1h before shower (65C safety cap)
                win = t(k)>=ss-manual_lead & t(k)<se;
                if win
                    if T(k)<63, on=true; elseif T(k)>65, on=false; end
                else
                    on=false; 
                end
                Pe(k)=on*Pel;
            case 'smart'                                    % continuous solar forecast -> precise top-up to 40C
                remS=max(0, solarByShower-cumQ(k));
                proj=T(k)+remS/mC - UA*(T(k)-Ta)*max(0,ss-t(k))/mC;
                t_need=mC*max(0,40-proj)/Pel;
                want=((ss-t(k))<=t_need+120) || (t(k)>=ss && t(k)<se);
                Pe(k)=(want && T(k)<40)*Pel;
        end
        mdot=flow(k)/60;
        if strcmp(mode,'smart') && flow(k)>0.1 && T(k)<Tc+1
            Pb(k)=Pbo; Del(k)=T(k)+Pb(k)/(mdot*Cp);
        else
            Del(k)=T(k); 
        end
        Qloss=UA*(T(k)-Ta); Qdraw=mdot*Cp*(T(k)-Tin);
        T(k+1)=T(k)+(Pe(k)+Q(k)-Qloss-Qdraw)/mC*dt;
    end
    Pe(end)=Pe(max(1,end-1)); Del(end)=Del(max(1,end-1));
    R.T=T; R.Pe=Pe; R.Del=Del; R.E=cumtrapz(t,Pe+Pb)/3.6e6; R.Eelec=R.E(end);
    sh=flow>0.1; R.Dmin=min(Del(sh));
end

function plot_temps(ax,th,D,LDR,ttl,Tc,ss,Esolar)
    axes(ax); hold on; grid on;
    yyaxis right; area(th,LDR,'FaceColor',[1 .85 .4],'FaceAlpha',.35,'EdgeColor','none','HandleVisibility','off');
    ylabel('Solar (LDR)'); ylim([0 4200]); ax.YAxis(2).Color=[.8 .55 0];
    yyaxis left; ax.YAxis(1).Color='k';
    mask=double(D.S.Pe>0); band=15+60*mask;
    area(th,band,'BaseValue',15,'FaceColor',[.3 .8 .3],'FaceAlpha',.18,'EdgeColor','none','HandleVisibility','off');
    plot(th,D.E.T,'r-','LineWidth',1.5);
    plot(th,D.C.T,'Color',[.9 .55 .1],'LineStyle','--','LineWidth',1.5);
    plot(th,D.S.T,'g-','LineWidth',1.9);
    xline(ss/3600,'c-','LineWidth',1.1,'HandleVisibility','off');
    ylabel('Tank Temp [\circC]'); ylim([15 75]); xlim([0 24]); xticks(0:3:24); title(ttl);
    text(0.02,0.95,sprintf('Solar harvested: %.2f kWh (free)',Esolar),'Units','normalized', ...
         'FontWeight','bold','Color',[.55 .38 0],'BackgroundColor','w','EdgeColor',[.8 .6 .2],'Margin',3);
    if any(mask)
        text(0.02,0.82,'green band = smart electric top-up','Units','normalized','FontSize',7,'Color',[.2 .55 .2]); 
    end
    legend({'Electric-only (1h)','Conv. solar (1h)','Smart hybrid'},'Location','northwest','FontSize',8);
end

function plot_energy(ax,th,D,ttl)
    axes(ax); hold on; grid on;
    plot(th,D.E.E,'r-','LineWidth',1.6);
    plot(th,D.C.E,'Color',[.9 .55 .1],'LineStyle','--','LineWidth',1.5);
    plot(th,D.S.E,'g-','LineWidth',1.9);
    ylabel('Cum. Electric [kWh]'); xlabel('Hour of day'); xlim([0 24]); xticks(0:3:24); title(ttl);
    legend(sprintf('Electric-only: %.2f',D.E.E(end)), sprintf('Conv solar: %.2f',D.C.E(end)), ...
           sprintf('Smart: %.2f',D.S.E(end)),'Location','northwest','FontSize',8);
end

function o=ternary(c,a,b), if c, o=a; else, o=b; end, end
function save_fig(f,p), try, exportgraphics(f,p,'Resolution',170); catch, print(f,p,'-dpng','-r170'); end, end