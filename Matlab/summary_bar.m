function summary_bar(T)
% summary_bar — Smart vs Dumb electrical energy for the 4 energy-focused scenarios.
%   summary_bar()      runs Scenarios 1,2,3,5 and collects the results.
%   summary_bar(T)      uses the table returned by summary_table() (no re-run).
close all;
scripts = { 'Scenario_1_Preheating','Scenario_2_ColdShowerStart', ...
    'Scenario_3_WarmShowerCutoff','Scenario_5_SolarBypass' };
labels  = { 'Pre-Heating','Cold Shower','Warm Shower','Solar Bypass' };
n = numel(scripts);
Esmart = nan(n,1); Edumb = nan(n,1);

if nargin>=1 && istable(T)                       
    for i=1:n
        row = find(contains(string(T.Scenario), labels{i}), 1);
        if ~isempty(row), Esmart(i)=T.Smart(row); Edumb(i)=T.Dumb(row); end
    end
else                                             
    for i=1:n
        fprintf('\n########## %s ##########\n', scripts{i});
        try
            evalin('base', ['run(''' scripts{i} '.m'')']);
            K = evalin('base','LAST_KPI');
            Esmart(i)=K.E_smart_kWh; Edumb(i)=K.E_dumb_kWh;
        catch ME
            warning('summary_bar:fail','%s: %s', scripts{i}, ME.message);
        end
    end
end
saved = (1 - Esmart./Edumb)*100;

% ---------- plot ----------
fig = figure('Color','w','Position',[80 100 960 580]);
Y = [Esmart, Edumb];
b = bar(Y, 'grouped', 'BarWidth', 0.85); hold on;
b(1).FaceColor = [0.20 0.45 0.85];  b(1).EdgeColor='none';   % Smart (blue)
b(2).FaceColor = [0.85 0.25 0.25];  b(2).EdgeColor='none';   % Dumb  (red)

set(gca,'XTick',1:n,'XTickLabel',labels,'FontSize',11,'LineWidth',1);
ylabel('Energy Consumption [kWh]','FontSize',12,'FontWeight','bold');
title('Smart vs Conventional Boiler — Electrical Energy per Scenario', ...
    'FontSize',13,'FontWeight','bold');
legend({'Smart Boiler','Dumb Boiler (65\circC)'},'Location','northwest','FontSize',11);
grid on; box off;
ymax = max(Y(:),[],'omitnan'); if isempty(ymax)||ymax==0, ymax=1; end
ylim([0 ymax*1.28]);

% value labels on top of every bar
for k=1:2
    xt=b(k).XEndPoints; yt=b(k).YEndPoints;
    for j=1:n
        if ~isnan(yt(j))
            text(xt(j), yt(j)+ymax*0.015, sprintf('%.2f',yt(j)), ...
                'HorizontalAlignment','center','VerticalAlignment','bottom', ...
                'FontSize',9,'FontWeight','bold','Color',[0.25 0.25 0.25]);
        end
    end
end

% "% Saved" callout above each group
for j=1:n
    if ~isnan(saved(j))
        xc = mean([b(1).XEndPoints(j), b(2).XEndPoints(j)]);
        yc = max([b(1).YEndPoints(j), b(2).YEndPoints(j)]) + ymax*0.10;
        text(xc, yc, sprintf('Saved: %.1f%%', saved(j)), ...
            'HorizontalAlignment','center','FontSize',10,'FontWeight','bold', ...
            'Color',[0 0.5 0],'BackgroundColor',[0.92 1 0.92], ...
            'EdgeColor',[0 0.5 0],'Margin',3);
    end
end

% ---------- save ----------
if ~exist('figures','dir'), mkdir figures; end
try
    exportgraphics(fig, fullfile('figures','summary_energy_bar.png'),'Resolution',200);
catch
    print(fig, fullfile('figures','summary_energy_bar.png'),'-dpng','-r200');
end
fprintf('\nSaved figures/summary_energy_bar.png\n');
end