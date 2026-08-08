function T = summary_table()
% Run all 8 scenarios, collect KPIs, print a table, render + save figures/summary_kpi_table.png
close all; clc;
scripts = { 'Scenario_1_Preheating','Scenario_2_ColdShowerStart', ...
    'Scenario_3_WarmShowerCutoff','Scenario_4_RedundantRequest', ...
    'Scenario_5_SolarBypass','Scenario_6_PLC_CommLoss', ...
    'Scenario_7_Overtemp','Scenario_8_StuckSSR' };
K = cell(1,numel(scripts));
for i = 1:numel(scripts)
    fprintf('\n########## RUNNING %s ##########\n', scripts{i});
    try
        evalin('base', ['run(''' scripts{i} '.m'')']);   
        K{i} = evalin('base','LAST_KPI');
    catch ME
        warning('summary:fail','%s: %s', scripts{i}, ME.message);
        K{i} = struct('name',scripts{i});
    end
end

n = numel(K);
Scenario=strings(n,1); Dur=nan(n,1); FinalT=nan(n,1); PeakT=nan(n,1); Ttgt=nan(n,1);
Smart=nan(n,1); Dumb=nan(n,1); Saved=nan(n,1); Useful=nan(n,1); Eta=nan(n,1); Liters=nan(n,1);
Event=strings(n,1);
for i=1:n
    s=K{i};
    Scenario(i)=string(gf(s,'name',scripts{i}));
    Dur(i)=gf(s,'dur',NaN); FinalT(i)=gf(s,'finalT',NaN); PeakT(i)=gf(s,'peakT',NaN);
    Ttgt(i)=gf(s,'t_to_target',NaN); Smart(i)=gf(s,'E_smart_kWh',NaN);
    Dumb(i)=gf(s,'E_dumb_kWh',NaN); Saved(i)=gf(s,'saved_pct',NaN);
    Useful(i)=gf(s,'E_useful_kWh',NaN); Eta(i)=gf(s,'eta',NaN);
    Liters(i)=gf(s,'liters_comfort',NaN); Event(i)=string(gf(s,'event',''));
end
T = table(Scenario,Dur,FinalT,PeakT,Ttgt,Smart,Dumb,Saved,Useful,Eta,Liters,Event);
fprintf('\n================= KPI SUMMARY =================\n'); disp(T);

cols = {'Scenario','Dur[s]','Final[C]','Peak[C]','t2tgt[s]','Smart[kWh]','Dumb[kWh]', ...
    'Saved[%]','Useful[kWh]','Eta','L>=38C','Event'};
C = cell(n,numel(cols));
for i=1:n
    C(i,:) = { char(Scenario(i)), f0(Dur(i),0), f0(FinalT(i),1), f0(PeakT(i),1), ...
        f0(Ttgt(i),0), f0(Smart(i),3), f0(Dumb(i),3), f0(Saved(i),1), ...
        f0(Useful(i),3), f0(Eta(i),2), f0(Liters(i),1), char(Event(i)) };
end
fig=figure('Color','w','Position',[60 120 1280 360]);
uitable(fig,'Data',C,'ColumnName',cols,'RowName',[], ...
    'Units','normalized','Position',[0.01 0.01 0.98 0.98],'FontSize',10);
if ~exist('figures','dir'), mkdir figures; end
try
    exportgraphics(fig, fullfile('figures','summary_kpi_table.png'), 'Resolution',150);
catch
    print(fig, fullfile('figures','summary_kpi_table.png'), '-dpng','-r150');
end
fprintf('\nSaved figures/summary_kpi_table.png\n');
end

function v = gf(s,f,d)          
if isstruct(s) && isfield(s,f) && ~isempty(s.(f)), v=s.(f); else, v=d; end
end
function str = f0(x,p)          
if isempty(x) || (isnumeric(x) && isnan(x)), str='-';
else, str=sprintf(['%.' num2str(p) 'f'], x); end
end