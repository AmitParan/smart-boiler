% =========================================================
% Smart Boiler Project - Scenario 2: Energy Optimized Shower
% =========================================================
clear; clc;
Flow_Min_LPM = 1;

%% 1. Tank Parameters (150L - Low Temp Storage)
Water_Volume_L = 150;              
Water_Mass_kg = Water_Volume_L;    
P_Internal_W = 2500;               
h_Internal = 0.8;                  
A_Internal = 1.6;                  

%% 2. Inline Boost Parameters (The 7-Degree Kick)
P_Boost_W = 3000;                  
h_Boost = 10; 
A_Boost = 0.1;

%% 3. Temperatures (Setpoint Dictionary)
T_Water_Inlet_C = 20;              
T_Initial_C = 30;      
T_Ambient_C = 18;      

% אלו המשתנים שבלוק 5 מחפש עכשיו:
T_target_logic  = 35;   % יעד "חסכוני" למקלחת בתרחיש זה
T_solar         = 25;   % יעד ליום שמשי (רצפת נוחות)
T_standby       = 20;   % יעד למצב המתנה/לילה

%% 4. Safety Logic & Thresholds
T_Max_Cutoff_C = 85;               
Flow_Min_LPM = 1;      
LDR_Day_Threshold = 70;            
Solar_Gain_Coeff = 4;  

%% 5. Real-Time & Communication
RTOS_Tick_Time = 0.01;             
Communication_Delay = 0.05;        
Noise_Level = 0.05;                

%% 6. TIME SERIES GENERATION (Scenario 2 - Hybrid Event)
t_end = 12000;                     
t_vector = (0:1:t_end)'; 

% א. LDR - מעונן חלקית (40)
ldr_data = ones(size(t_vector)) * 40;

% ב. Habit - דרישה למקלחת פעילה (1)
habit_data = ones(size(t_vector)) * 1;

% ג. Flow - מקלחת של 10 דקות החל מ-6000 שניות
flow_data = zeros(size(t_vector));
start_shower = 6000;
end_shower = 6000 + (10 * 60); 
flow_data(start_shower:end_shower) = 6; 

sim_LDR   = timeseries(ldr_data, t_vector);
sim_Habit = timeseries(habit_data, t_vector);
sim_Flow  = timeseries(flow_data, t_vector);

fprintf('====================================================\n');
fprintf('   SYSTEM READY: SCENARIO 2 (HYBRID HEATING)        \n');
fprintf('   Tank Target: 35C | T_solar: 25C | T_standby: 20C \n');
fprintf('====================================================\n');

% sim('Boiler_System_Model');