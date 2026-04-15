% =========================================================
% Smart Boiler Project - Scenario 3: Habit Learning Logic
% =========================================================
clear; clc;
Flow_Min_LPM = 1;

%% 1. Tank Parameters (Fixed)
Water_Volume_L = 150; Water_Mass_kg = 150;
P_Internal_W = 2500; h_Internal = 0.8; A_Internal = 1.6;

%% 2. Boost Parameters
P_Boost_W = 3000; h_Boost = 10; A_Boost = 0.1;

%% 3. Temperatures (The Setpoint Dictionary)
T_Water_Inlet_C = 20; 
T_Initial_C     = 20;  % מתחילים ממים קרים בבוקר
T_Ambient_C     = 15;  

T_target_logic  = 35;  % יעד המקלחת (Habit = 1)
T_solar         = 25;  % יעד לזמן שיש שמש אבל אין מקלחת
T_standby       = 20;  % יעד למצב המתנה (לילה)

%% 4. Safety Logic & Thresholds
T_Max_Cutoff_C = 85; Flow_Min_LPM = 1; Solar_Gain_Coeff = 3; 

%% 5. Time Series Generation (The "Learning" Scenario)
t_end = 20000; 
t_vector = (0:1:t_end)';

% א. LDR - חורף (עננות בינונית)
sim_LDR = timeseries(ones(size(t_vector)) * 40, t_vector);

% ב. Habit - המערכת "למדה" שאתה מתקלח בערב (בין 12,000 ל-18,000 שניות)
habit_data = zeros(size(t_vector));
habit_data(12000:18000) = 1; 
sim_Habit = timeseries(habit_data, t_vector);

% ג. Flow - אין זרימה (רק הכנה של המים)
sim_Flow = timeseries(zeros(size(t_vector)), t_vector);

fprintf('====================================================\n');
fprintf('   SYSTEM READY: SCENARIO 3 (HABIT LEARNING)        \n');
fprintf('   Heating should start ONLY at t=12000s            \n');
fprintf('====================================================\n');