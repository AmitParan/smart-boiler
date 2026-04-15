# Smart Hybrid Boiler Control System — MATLAB/Simulink Simulation

## Overview

This folder contains the MATLAB/Simulink simulation for the Smart Hybrid Boiler Control System.
The project models realistic thermodynamic behaviour of a domestic boiler using a **Simulink physical model** combined with a **state-machine logic controller** that decides between energy sources based on solar availability, time patterns, and user habits.

## Key Features

- **Passive Solar Prioritization** — uses solar energy whenever available before activating the electric/gas heater.
- **Inline Rapid Heating (Boost)** — activates the heater in a controlled burst to reach target temperature quickly.
- **Predictive Habit Learning** — tracks usage patterns to pre-heat water before the expected demand window.

## Folder Structure

```
matlab/
├── Models/      # Core Simulink model (.slx)
├── Scripts/     # MATLAB scenario scripts (.m)
├── Docs/        # Diagrams and reports (.pdf)
└── Images/      # Simulation screenshots for reference
```

## Test Scenarios

| Scenario | File | Description |
|---|---|---|
| 1 | `Scenario1_Solar.m` | Passive solar day — heater stays off |
| 2 | `Scenario2_WinterShower.m` | Cold winter morning — hybrid boost activated |
| 3 | `Scenario3_Dynamic.m` | Dynamic load — controller switches modes in real time |
| 4 | `Scenario4_Safety.m` | Safety cutoff — over-temperature protection |
| 5 | `Scenario5_fastheating.m` | Fast heating demand — rapid boost sequence |
| 6 | `Scenario6_familystress.m` | Family peak stress test — multiple simultaneous demands |

## Usage

1. Clone the repository:
   ```bash
   git clone https://github.com/AmitParan/smart-boiler.git
   ```
2. Open MATLAB and navigate to the `matlab/Scripts/` folder.
3. Open `matlab/Models/Boiler_System_Model.slx` in Simulink.
4. Run any scenario script, for example:
   ```matlab
   run('Scenario1_Solar.m')
   ```
5. Simulation results and graphs will appear in the MATLAB figure window.
