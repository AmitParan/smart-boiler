% =========================================================
% Smart Boiler Project - Scenario 6: Family Stress Test
% =========================================================
clear; clc;
Flow_Min_LPM=0;
%% 1. Tank Parameters (150L Family Tank)
Water_Volume_L = 150;              
Water_Mass_kg = Water_Volume_L;    
P_Internal_W = 2500;               
h_Internal = 0.8;                  
A_Internal = 1.6;                  

%% 2. Boost Parameters (JD30 - 3000W Inline)
P_Boost_W = 3000;                  
h_Boost = 10; 
A_Boost = 0.1;

%% 3. Temperatures 
T_Water_Inlet_C = 20;              
T_Initial_C = 37;      % המיכל כבר הגיע ליעד ומוכן למקלחת הראשונה
T_Ambient_C = 15;      % ערב חורפי (הפסדי חום מוגברים לסביבה)

% הגדרות בקר:
T_target_logic  = 37;  
T_solar         = 25;  
T_standby       = 20;  

%% 4. Safety Logic & Thresholds
T_Max_Cutoff_C = 85;               
Flow_Min_LPM = 1;                  
LDR_Day_Threshold = 70;            
Solar_Gain_Coeff = 2;              

%% 5. Real-Time & Communication
RTOS_Tick_Time = 0.01;             
Communication_Delay = 0.05;        
Noise_Level = 0.05;                

%% 6. TIME SERIES GENERATION (Consecutive Showers)
t_end = 8000;                      
t_vector = (0:1:t_end)'; 

% א. LDR - לילה (אין חימום סולארי פסיבי)
sim_LDR = timeseries(ones(size(t_vector)) * 10, t_vector);

% ב. Habit - דרישה רציפה (שעות הערב הפעילות של הבית)
sim_Habit = timeseries(ones(size(t_vector)) * 1, t_vector);

% ג. Flow - שתי מקלחות ארוכות של 20 דקות (1200 שניות כל אחת)
flow_data = zeros(size(t_vector));

% מקלחת מס' 1:
flow_data(1000:2200) = 6; 

% הפסקה של 15 דקות (900 שניות) להתארגנות...

% מקלחת מס' 2:
flow_data(3100:4300) = 6; 

sim_Flow = timeseries(flow_data, t_vector);

fprintf('====================================================\n');
fprintf('   SYSTEM READY: SCENARIO 6 (STRESS TEST)           \n');
fprintf('   Shower 1: 1000s-2200s | Shower 2: 3100s-4300s    \n');
fprintf('   Watch the T_Internal drop and the Boost kick in! \n');
fprintf('====================================================\n');

% sim('Boiler_System_Model');