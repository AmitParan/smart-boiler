% =========================================================
% Smart Boiler Project - Scenario 1: Summer Passive Solar
% =========================================================
clear; clc;
Flow_Min_LPM = 1;
%% 1. Tank Parameters (150L Family Tank)
Water_Volume_L = 150;              
Water_Mass_kg = Water_Volume_L;    
P_Internal_W = 2500;               
h_Internal = 0.8;                  
A_Internal = 1.6;                  

%% 2. Boost Parameters (JD30 - 3000W)
P_Boost_W = 3000;                  
h_Boost = 10; 
A_Boost = 0.1;

%% 3. Temperatures 
T_Water_Inlet_C = 20;              
T_Initial_C = 20;                  
T_Ambient_C = 20;                  
T_target_logic = 25;   % יעד נמוך לחימום שמש בלבד
T_solar = 40;          % המודל דורש את המשתנה הזה
T_standby = 30;        % המודל דורש את המשתנה הזה

%% 4. Safety Logic & Thresholds
T_Max_Cutoff_C = 85;               
Flow_Min_LPM = 1;                  
LDR_Day_Threshold = 70;            
Solar_Gain_Coeff = 8;              

%% 5. Real-Time & Communication
RTOS_Tick_Time = 0.01;             
Communication_Delay = 0.05;        
Noise_Level = 0.05;                

%% 6. TIME SERIES GENERATION (Scenario Data)
t_end = 10000;                     
t_vector = (0:1:t_end)'; 

% נתוני קיץ: שמש חזקה (LDR=90), אין מקלחת, אין זרימה
ldr_data   = ones(size(t_vector)) * 90; 
habit_data = ones(size(t_vector)) * 0;
flow_data  = ones(size(t_vector)) * 0;

sim_LDR   = timeseries(ldr_data, t_vector);
sim_Habit = timeseries(habit_data, t_vector);
sim_Flow  = timeseries(flow_data, t_vector);

fprintf('====================================================\n');
fprintf('   SYSTEM READY: SCENARIO 1 (PASSIVE SUMMER)        \n');
fprintf('   Target: 25C | LDR: 90 | Electricity should be OFF \n');
fprintf('====================================================\n');

% sim('Boiler_System_Model'); % בטל את ה-comment כדי להריץ אוטומטית