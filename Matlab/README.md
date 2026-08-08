========================================================================
             SMART ELECTRIC BOILER -- SIMULATION SUITE
========================================================================

MATLAB / Simulink / Simscape models and analytical studies for a 
retrofit Smart Electric Boiler. The goal is to reduce electricity 
consumption WITHOUT reducing user comfort by heating only when needed, 
keeping a low standby temperature, preheating before predicted use, 
and using an inline boost heater on demand.

This suite validates the thermal model, runs 8 scenarios, and produces 
committee-ready figures and KPI tables.


------------------------------------------------------------------------
1. SYSTEM UNDER TEST
------------------------------------------------------------------------

+-------------------+----------------------------------+------------------------------------+
| Component         | Spec                             | Role                               |
+-------------------+----------------------------------+------------------------------------+
| Water tank        | 150 L (150 kg), Cp=4184 J/(kg*K) | Main thermal reservoir             |
| Internal heater   | 2500 W                           | Heats the whole tank               |
| Inline boost      | 3000 W (Andetong JDU30, UA=33)   | Instant on-demand lift of flow     |
| Temp sensor       | DS18B20                          | Tank / delivered temperature       |
| Flow sensor       | YF-B6                            | Detects draw (LPM)                 |
| Current sensor    | ACS758                           | Power metering + stuck-SSR check   |
| Controller        | ESP32 + FreeRTOS                 | Smart control logic                |
| Comms             | PLC Master <-> Slave             | Distributed control (packet loss)  |
| Actuators         | SSRs (internal + boost)          | Switch the heaters                 |
| Safety            | Flow interlock, SW/HW over-temp  | Fail-safe protection               |
+-------------------+----------------------------------+------------------------------------+

Key Physical Constants (defined in boiler_params.m):
- UA_Losses = 1.5 W/K (Rth = 0.667 K/W)
- Ambient Temperature = 20 degrees C
- Comfort Threshold = 38 degrees C
- Supply Voltage = 230 V
- Standby Target = 30 degrees C
- Active Target = 40 degrees C
- Conventional Target = 65 degrees C


------------------------------------------------------------------------
2. REPOSITORY STRUCTURE
------------------------------------------------------------------------

BoilerProject/
|-- boiler_params.m            % SINGLE source of truth for constants (load first)
|-- run_all.m                  % Runs Scenarios 1-8 sequentially, saves all figures
|-- summary_table.m            % Aggregates all-scenario KPIs into a table figure
|-- summary_bar.m              % Smart vs Dumb energy bar (energy scenarios)
|-- energy_philosophy.m        % Analytical 24h real-life "why we save" study (3 cases)
|-- Scenario_1_Preheating.m
|-- Scenario_2_ColdShowerStart.m
|-- Scenario_3_WarmShowerCutoff.m
|-- Scenario_4_RedundantRequest.m
|-- Scenario_5_SolarBypass.m
|-- Scenario_6_PLC_CommLoss.m
|-- Scenario_7_Overtemp.m
|-- Scenario_8_StuckSSR.m
|-- lib/
|   |-- pack_boiler_params.m   % Snapshots workspace constants into struct P
|   |-- getSig.m               % Robust logged-signal fetch (duplicate-name safe)
|   |-- extract_signals.m      % Pulls model signals into a struct S
|   |-- dumb_baseline.m        % Conventional 65C boiler reference model
|   |-- plot_boiler_std.m      % Standard 4-row scenario figure plotting tool
|   +-- kpi_report.m           % Computes + prints KPIs, publishes LAST_KPI
|-- figures/                   % Auto-saved PNGs for report and presentation slides
+-- Boiler_System_Model.slx    % Simscape thermal + control model


------------------------------------------------------------------------
3. REQUIREMENTS
------------------------------------------------------------------------

- MATLAB R2020a+ (uses exportgraphics, XEndPoints; falls back to print if older)
- Simulink + Simscape (required for Scenarios 1-4 which run Boiler_System_Model)
- Scenarios 5-8 and analytical studies are PURE MATLAB (Simulink model not required)


------------------------------------------------------------------------
4. QUICK START
------------------------------------------------------------------------

In the MATLAB Command Window:

    >> cd BoilerProject
    >> run_all             % Runs all 8 scenarios and saves PNGs to figures/
    >> summary_table       % Generates cross-scenario KPI evaluation table
    >> summary_bar         % Produces comparative energy bar chart
    >> energy_philosophy   % Runs the 24h analytical energy-saving proof

Note: Every scenario script begins by loading boiler_params, ensuring 
constants are always defined and preventing workspace cleanup errors.


------------------------------------------------------------------------
5. MODEL INTERFACE (SCENARIOS 1-4)
------------------------------------------------------------------------

Inputs (base-workspace timeseries): 
- sim_Flow (LPM)
- sim_LDR (0-1023)
- sim_Habit (0/1 predicted-use)
- sim_User_Mode (1 = manual/active, 2 = auto)

Logged signals: 
- T_Internal, T_Boost, Cmd_Internal, Cmd_Boost, Safe_Internal, P_Internal_Actual_W

PLC Packet Loss Control:
Controlled by Block_3_PLC_Channel/Switch threshold:
- -1   = 0% loss (used for all clean thermodynamic runs)
- 0.05 = approx. 52% loss (raw model default)
- 1.1  = 100% full communication loss

Model-Frozen Policy:
The core Simulink file (.slx) is never modified. Fault behaviors, safety 
override conditions, and predictive solar logics for Scenarios 5-8 are 
fully emulated script-side by post-processing or overriding the struct S.


------------------------------------------------------------------------
6. STANDARD FIGURE LAYOUT (plot_boiler_std)
------------------------------------------------------------------------

Every model-based scenario script renders 4 synchronized rows on a dynamic 
time axis (scaled automatically to seconds, minutes, or hours):
Row 1: Main Reservoir Temperature (Smart tank, Dumb baseline, Target lines)
Row 2: Cumulative Electrical Energy (Smart vs Dumb) -- or Power for safety
Row 3: Water Flow & Delivered Temp (shaded booster Delta-T band during flow)
Row 4: Actuator / SSR Command States


------------------------------------------------------------------------
7. THE EIGHT SCENARIOS
------------------------------------------------------------------------

+---+-----------------------+----------+----------------------+--------------------------+
| # | Name                  | Duration | Focus                | Baseline Reference       |
+---+-----------------------+----------+----------------------+--------------------------+
| 1 | Pre-Heating           | ~4.2 h   | Energy Efficiency    | Dumb 65 degrees C        |
| 2 | Preheated Shower      | 10 min   | Comfort / Hot Water  | Passive (No heating)     |
| 3 | Warm Shower Cutoff    | 10 min   | Active Control       | Passive                  |
| 4 | Redundant Request     | 5 min    | Idle Behavior        | --                       |
| 5 | Predictive Solar      | 24 h     | Solar Intelligence   | Elec-only & Conv-solar   |
| 6 | PLC Comm Loss         | 60 s     | Comm Fail-Safe       | --                       |
| 7 | Redundant Safety      | accel.   | Multi-threshold SW/HW| --                       |
| 8 | Stuck SSR Lockout     | 0.5 s    | Current Diagnostics  | --                       |
+---+-----------------------+----------+----------------------+--------------------------+

* Scenario 1 -- Pre-Heating:
Heats stagnant 150 L tank to the 40C smart target and compares with 
conventional heating to 65C. Demonstrates validated thermal model and the 
>50% standby energy savings. Curves overlap until smart tank hits 40C.

* Scenario 2 -- Preheated Shower:
Smart system predicted shower and preheated tank to 40C. An 8 LPM draw 
occurs, and the 3000 W inline boost tops up the delivered temperature 
(Delta-T approx. 5.4C). Conventional boiler was off (cold water).
Key Metric: Delivered temperature and comfort %, not energy.

* Scenario 3 -- Warm Shower / Dynamic Cutoff:
Tank starts at 45C, active target is 42C. Boost remains OFF while tank 
alone is sufficient. As tank drops below 42C, the boost instantly snaps 
ON (shaded band appears). Internal element has no flow interlock (only over-temp).

* Scenario 4 -- Redundant Request (Standby):
Auto mode with no predicted use (sim_User_Mode = 2, sim_Habit = 0). System 
holds 30C standby state and ignores repetitive manual ON commands to avoid 
overheating.

* Scenario 5 -- Predictive Solar (Analytical, Sunny + Cloudy):
Three-way daily comparison with 19:00 shower:
- Electric-only (1h manual): often results in cold shower (comfort failure)
- Conventional solar (1h blind manual): wastes electrical energy on sunny days
- Smart hybrid: continuously forecasts solar yield (remaining solar integration)
  and applies a precise electrical top-up only when needed.
Outputs: 2x2 plots, solar harvested indicators, green electric-pulse bands.

* Scenario 6 -- PLC Communication Loss:
Timeline: idle -> active heating -> sudden comm loss. Proves fail-safe. 
When the PLC link drops, the user demand remains active but the safety 
interlock immediately drops power to 0W. Resolution: 50 ms.

* Scenario 7 -- Thermal Runaway & Redundant Safety (Two Cases):
Tight 60-100C axis, dual-layer thresholds:
- Case 7A (SW success): software de-energizes the SSR at 80C.
- Case 7B (HW backup): software fails (stuck SSR), temperature climbs to 
  85C where physical hardware cutoff switch trips and cuts raw supply power.

* Scenario 8 -- Stuck SSR caught by Current Sensor (ACS758):
Electrical fail-safe layer. Software commands OFF but current continues 
to flow. The ACS758 current sensor detects the current-vs-command mismatch 
and trips an independent magnetic lockout relay in 50 ms -- preventing 
thermal runaway before any temperature rise occurs.


------------------------------------------------------------------------
8. ANALYTICAL STUDY -- energy_philosophy.m
------------------------------------------------------------------------

Model-independent, thermodynamic (m*Cp*dT/dt) 24h simulation analyzing 
three real-life residential behaviors (event-driven, boiler OFF between showers):
1. Dumb Wasteful: Switched on early, heats tank to 65C. Comfort OK, but 
   high standby losses and severe overheating.
2. Dumb Cold: Switched on only 1h before use. Inefficient, results in cold water.
3. Smart Hybrid: Precise preheat to 40C combined with inline boost. Comfort OK, 
   very low energy usage.

Produces diverging cumulative energy plots, an exact 4-way stacked energy 
breakdown (standing losses, useful delivered at 38C, over-heat waste, 
and residual stored energy), and a KPI comparison table.


------------------------------------------------------------------------
9. KPI DEFINITIONS
------------------------------------------------------------------------

- Smart / Dumb Energy [kWh]: Integral of (P_internal + P_boost) dt.
- % Saved: 1 - E_smart / E_dumb (suppressed when dumb baseline is passive).
- Useful Delivered [kWh]: Integral of mdot*Cp*(min(T_deliver, 38) - T_inlet) dt 
  during flow periods (comfort normalized).
- Comfort %: Fraction of active flow time where T_deliver >= 38 degrees C.
- Boost Delta-T: T_deliver - T_Internal during flow (= P_boost / (mdot*Cp)).
- Sheath Temp: T_water + P_boost/UA_boost (thick-film safety limits).
- Trip Latency: Elapsed time from fault onset to safety lockout (Scenarios 7/8).


------------------------------------------------------------------------
10. THERMAL MODEL VALIDATION
------------------------------------------------------------------------

Simscape tank validated against the exact analytic solution of the 
continuous ordinary differential equation: m*Cp*dT/dt = P - UA*(T - T_amb)
with a constant 2500W heating input:
- RMSE (Simscape vs Analytic) < 0.5 degrees C
- Injected power verified at 2500W via logged P_Internal_Actual_W
- Verified losses: 0W at start, 30W at 40C (UA = 1.5 W/K)

Fixed bugs documented during validation:
- Heater block gain fixed to P_Internal_W, not P_Internal_W/2500.
- Convective coefficient (h_Internal) raised to 1000 W/(m^2*K) to ensure 
  effective thermal coupling with the water.
- Ambient thermal reference modified to absolute Kelvin (293.15 K), not 20.
- PLC channel packet-loss switch threshold set to -1 (0% loss) for standard runs.
- Addressed duplicate Cmd_Internal logging in getSig.m.


------------------------------------------------------------------------
11. TUNING KNOBS (LOCATED AT THE TOP OF EACH SCRIPT)
------------------------------------------------------------------------

- Scenarios 2 & 3: T_tank_start, FLOW_LPM, T_target_logic
- Scenario 5: Qsun, Qcld (solar peaks), manual_lead
- Scenario 7: ramp (accelerated rate), Tsw = 80, Thw = 85
- Scenario 8: t_cmd, t_trip (50 ms lockout delay), I_nom
- energy_philosophy: showers, dur, FLOW, user_margin


------------------------------------------------------------------------
12. OUTPUTS
------------------------------------------------------------------------

Figures are automatically written to the figures/ directory as high-resolution 
PNG files (170-200 DPI). Real-time KPI summaries are printed to the command 
line during execution. Run summary_table and summary_bar to compile 
final aggregated presentation graphics.

========================================================================