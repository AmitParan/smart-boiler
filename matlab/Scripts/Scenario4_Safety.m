% =========================================================
% Smart Boiler Project - Scenario 4: Safety Protection Test
% =========================================================
clear; clc;
Flow_Min_LPM = 1;

%% 1. Tank Parameters
Water_Volume_L = 150; Water_Mass_kg = 150;
P_Internal_W = 2500; h_Internal = 0.8; A_Internal = 1.6;

%% 2. Boost Parameters
P_Boost_W = 3000; h_Boost = 10; A_Boost = 0.1;

%% 3. Setpoint Dictionary (The Stress Test)
T_Water_Inlet_C = 20; 
T_Initial_C     = 20;  

T_target_logic  = 100; % דרישה לא מציאותית לבדיקת הגנות
T_solar         = 25;  
T_standby       = 20;  

%% 4. Safety Logic & Thresholds
T_Max_Cutoff_C    = 85;  % כאן המערכת חייבת לחתוך!
Flow_Min_LPM      = 1; 
Solar_Gain_Coeff  = 3; 

%% 5. Time Series Generation
t_end = 35000; % זמן ארוך כדי להגיע לטמפרטורה גבוהה
t_vector = (0:1:t_end)';

sim_LDR   = timeseries(ones(size(t_vector)) * 20, t_vector);
sim_Habit = timeseries(ones(size(t_vector)) * 1, t_vector); % דרישה קבועה
sim_Flow  = timeseries(zeros(size(t_vector)), t_vector);

fprintf('====================================================\n');
fprintf('   SYSTEM READY: SCENARIO 4 (SAFETY TEST)           \n');
fprintf('   Target: 100C | Cutoff expected at 85C            \n');
fprintf('====================================================\n');