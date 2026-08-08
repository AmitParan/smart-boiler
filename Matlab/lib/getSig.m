function y = getSig(out, name, t)
% Robustly fetch a logged signal onto time base t (handles duplicate names).
y = [];
try
    el  = out.logsout.getElementNames();
    idx = find(strcmp(strtrim(el), strtrim(name)));
    if isempty(idx), return; end
    v   = out.logsout{idx(1)}.Values;
    tt  = v.Time(:); dd = squeeze(double(v.Data));
    [tt, ia] = unique(tt, 'stable'); dd = dd(ia);
    y = interp1(tt, dd, t(:), 'linear', 'extrap');
catch
    % Fail silently if signal does not exist
end
end