function [fig, xs] = plot_boiler_std(S, cfg)
% Temp / Energy(or Power) / Flow+Boost delta / Commands. Dynamic x-axis. cfg.show_power for safety.
    fig = figure('Name',cfg.name,'Color','w','Position',[80 40 920 820]);
    t = S.t; tmax = t(end);
    if     tmax>=3600, xs=3600; xu='hours';
    elseif tmax>=300,  xs=60;   xu='minutes';
    else,              xs=1;    xu='seconds'; end
    tx = t/xs; hasFlow = any(S.flow > 0.1);

    % ===== 1: Reservoir temperature =====
    subplot(4,1,1); hold on; grid on;
    h=[]; L={};
    h(end+1)=plot(tx,S.T_Internal,'b-','LineWidth',2.0); L{end+1}='Smart Tank (Our System)';
    if isfield(cfg,'T_dumb') && ~isempty(cfg.T_dumb)
        dumb_lbl='Dumb Tank (65\circC baseline)';
        if isfield(cfg,'dumb_label') && ~isempty(cfg.dumb_label), dumb_lbl=cfg.dumb_label; end
        h(end+1)=plot(tx,cfg.T_dumb,'r--','LineWidth',1.2); L{end+1}=dumb_lbl;
    end
    if isfield(cfg,'show_target_line') && cfg.show_target_line && isfield(cfg,'target')
        h(end+1)=yline(cfg.target,'g:','LineWidth',1.5);
        L{end+1}=sprintf('Booster Threshold (%.0f\\circC)', cfg.target);
    end
    allT=[S.T_Internal(:);80]; if isfield(cfg,'T_dumb')&&~isempty(cfg.T_dumb), allT=[allT;cfg.T_dumb(:)]; end
    ylim([0 max(80,1.1*max(allT))]);
    ylabel('Temperature [\circC]'); title([cfg.name '  |  Main Reservoir Temperature']);
    legend(h,L,'Location','best');

    % ===== 2: Energy comparison OR power+energy (safety) =====
    subplot(4,1,2); hold on; grid on;
    if isfield(cfg,'show_power') && cfg.show_power
        yyaxis left;  plot(tx,S.P_elec,'b-','LineWidth',1.8); ylabel('Power [W]');
        ylim([0 max(100,1.2*max(S.P_elec))]);
        yyaxis right; plot(tx,S.E_kWh,'r-','LineWidth',1.4);  ylabel('Energy [kWh]');
        title('Instantaneous Power & Cumulative Energy');
        legend('Instantaneous Power [W]','Cumulative Energy [kWh]','Location','best');
    else
        plot(tx,S.E_kWh,'b-','LineWidth',2.0);
        if isfield(cfg,'P_dumb') && ~isempty(cfg.P_dumb)
            plot(tx, cumtrapz(t,cfg.P_dumb)/3.6e6,'r--','LineWidth',1.6);
            legend('Smart Boiler (Our System)','Dumb Boiler (Conventional Baseline)','Location','best');
        else
            legend('Smart Boiler (Our System)','Location','best');
        end
        ylabel('Energy Spent [kWh]'); title('Cumulative Electrical Energy Consumption');
    end

    % ===== 3: Delivered temperature =====
    subplot(4,1,3); hold on; grid on;
    if hasFlow
        yyaxis left;
        hF=plot(tx,S.flow,'-','Color',[0 .5 0],'LineWidth',1.6); ylabel('Flow Rate [LPM]');
        ylim([-0.5 max(1,1.2*max(S.flow))]);

        yyaxis right;
        fa = S.flow>0.1;
        Xb=[tx(fa); flipud(tx(fa))]; Yb=[S.T_Internal(fa); flipud(S.T_deliver(fa))];
        fill(Xb,Yb,[1 .82 .5],'EdgeColor','none','FaceAlpha',0.55);        % shaded boost lift
        hL=plot(tx,S.T_Internal,'b-','LineWidth',1.8);                      % conventional delivered
        hU=plot(tx,S.T_deliver ,'m-','LineWidth',1.8);                      % smart delivered
        ylabel('Delivered Temperature [\circC]'); ylim([0 max(50,1.1*max(S.T_deliver))]);
        title('Delivered Water: Conventional (tank only) vs Smart (tank + inline boost)');
        legend([hF hL hU],{'Water Flow [LPM]','Conventional Delivered (no boost)', ...
               'Smart Delivered (tank + boost)'},'Location','best');
        
        % Compute boost lift delta directly from temperatures
        dTb = S.T_deliver - S.T_Internal;
        if any(fa)
            text(0.65, 0.85, sprintf('Boost \\DeltaT \\approx %.1f\\circC', max(dTb(fa))), ...
                'Units','normalized','FontWeight','bold','BackgroundColor',[1 1 1], ...
                'EdgeColor',[.5 .5 .5],'Margin',3);
        end
    else
        plot(tx,S.flow,'-','Color',[0 .5 0],'LineWidth',1.6); ylabel('Flow Rate [LPM]');
        ylim([-0.5 5.0]); title('Water Flow Rate (No active consumption)');
        legend('Water Flow [LPM]','Location','best');
    end

    % ===== 4: Commands / SSR =====
    subplot(4,1,4); hold on; grid on;
    stairs(tx,S.cmd_internal,'b-','LineWidth',1.8);
    stairs(tx,S.cmd_boost,'m-','LineWidth',1.2);
    
    % Dynamic inclusion of User Request and Safety Relay in Legend
    leg_items = {'Internal Element SSR','Inline Boost SSR'};
    if isfield(S,'user_req') && ~isempty(S.user_req)
        stairs(tx,S.user_req,'Color',[0.9 0.5 0],'LineStyle','--','LineWidth',1.5);
        leg_items{end+1} = 'User App Request (0/1)';
    end
    if isfield(S,'safe_internal') && ~isempty(S.safe_internal) && any(S.safe_internal==0)
        stairs(tx,S.safe_internal,'r:','LineWidth',1.5);
        leg_items{end+1} = 'Safety Relay (1=OK)';
    end
    
    legend(leg_items,'Location','best');
    ylim([-0.1 1.2]); ylabel('Command State [0/1]');
    xlabel(sprintf('Time [%s]',xu)); title('Actuator Control Signals (SSR commands)');
end