% =========================================================
% Smart Boiler Project - Scenario 5: Guest Mode (Manual Override)
% =========================================================
clear; clc;
Flow_Min_LPM = 1;

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

%% 3. Temperatures (User Demand)
T_Water_Inlet_C = 20;              
T_Initial_C = 20;      % מים קרים (למשל צהריים ללא שמש)
T_Ambient_C = 20;      

% הגדרות לבלוק 5 הדינמי:
T_target_logic  = 40;  % יעד "חימום מהיר" למצב אורחים
T_solar         = 25;  
T_standby       = 20;  

%% 4. Safety Logic & Thresholds
T_Max_Cutoff_C = 85;               
Flow_Min_LPM = 1;                  
LDR_Day_Threshold = 70;            
Solar_Gain_Coeff = 3;              

%% 5. Real-Time & Communication
RTOS_Tick_Time = 0.01;             
Communication_Delay = 0.05;        
Noise_Level = 0.05;                

%% 6. TIME SERIES GENERATION (The Unexpected Event)
t_end = 8000;                      
t_vector = (0:1:t_end)'; 

% א. LDR - חורפי/מעונן
sim_LDR = timeseries(ones(size(t_vector)) * 30, t_vector);

% ב. Habit - המשתמש לוחץ על "בוסט" באפליקציה ב-t=1000 שניות
habit_data = zeros(size(t_vector));
habit_data(1000:end) = 1; 
sim_Habit = timeseries(habit_data, t_vector);

% ג. Flow - האורח נכנס למקלחת ב-t=5000 (שיא העומס!)
flow_data = zeros(size(t_vector));
flow_data(5000:6500) = 6; % מקלחת של 25 דקות
sim_Flow = timeseries(flow_data, t_vector);

fprintf('====================================================\n');
fprintf('   SYSTEM READY: SCENARIO 5 (GUEST MODE)            \n');
fprintf('   Manual Start: 1000s | Shower Starts: 5000s       \n');
fprintf('   Observe: 5500W combined power during shower!     \n');
fprintf('====================================================\n');

% sim('Boiler_System_Model');