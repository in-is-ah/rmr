# Lift Control System - Sequence Diagram

## Main Flow: Sending Floor Command

```mermaid
sequenceDiagram
    participant User
    participant ClientPi as Robot RMR (ESP)<br/>(WiFi router & LoRa device)
    participant RobotMainController as Robot Main Controller
    participant CameraModule as Robot Camera<br/>Raspberry Pi
    participant LiftService as Panel RMR (ESP)
    participant LiftHardware as Lift Hardware<br/>(Physical Mechanism)

    Note over User,LiftHardware: Floor Command Flow
    User->>ClientPi: You are in Floor 3,<br/>you need to go to Floor 5
    loop Polling Loop
        RobotMainController->>ClientPi: GET /api/pending-command<br/>(via WiFi)
        ClientPi-->>RobotMainController: {current: 3, target: 5} or null
    end
    RobotMainController->>RobotMainController: Re-confirm current location<br/>(Floor 3)
    RobotMainController->>RobotMainController: Move to Floor 3<br/>Robot Waiting Zone
    RobotMainController->>CameraModule: Position before lift<br/>(via ROS or REST ?)
    ClientPi->>LiftService: Go to Floor 3, pick up and go to Floor 5<br/>(via LoRa)
    LiftService->>LiftService: Validate floor number
    LiftService->>LiftService: Update lift_state<br/>(target_floor, status)
    LiftService-->>ClientPi: Command acknowledged
    
    Note over User,LiftHardware: Status Monitoring
    User->>ClientPi: Check lift status
    ClientPi->>LiftService: GET /api/status<br/>(via LoRa)
    LiftService-->>ClientPi: {current_floor: 0,<br/>target_floor: 5,<br/>status: "moving_up"}<br/>(via LoRa)
    ClientPi-->>User: Status update
    
    Note over User,LiftHardware: Lift Movement & Arrival
    LiftHardware->>LiftHardware: Move lift to floor 5
    LiftHardware->>LiftService: POST /api/arrived<br/>{floor: 5}
    LiftService->>LiftService: Update lift_state<br/>(current_floor: 5,<br/>status: "arrived")
    LiftService-->>LiftHardware: {status: "success",<br/>current_floor: 5}
    
    Note over User,LiftHardware: Final Status Check
    User->>ClientPi: Check if arrived
    ClientPi->>LiftService: GET /api/status<br/>(via LoRa)
    LiftService-->>ClientPi: {current_floor: 5,<br/>target_floor: null,<br/>status: "arrived"}<br/>(via LoRa)
    ClientPi-->>User: Lift arrived at floor 5
```

## Component Interaction Overview

```mermaid
graph TB
    subgraph "External"
        User[User]
    end
    
    subgraph "Client Side"
        ClientPi[Robot RMR / ESP - Runs client_example.py]
    end
    
    subgraph "Network Subgraph"
        Network[WiFi/Ethernet<br/>TCP/IP]
    end
    
    subgraph "Lift Control Side"
        LiftPi[Lift Control Pi<br/>Raspberry Pi #2<br/>In Lift Panel]
        LiftService[Panel RMR (ESP)<br/>app.py<br/>Port 5000]
        LiftState[(Lift State<br/>In-Memory)]
    end
    
    subgraph "Hardware"
        LiftHardware[Lift Hardware<br/>Motors, Sensors<br/>Physical Mechanism]
    end
    
    User -->|Commands| ClientPi
    ClientPi <-->|HTTP REST API| Network
    Network <-->|HTTP REST API| LiftPi
    LiftPi --> LiftService
    LiftService <--> LiftState
    LiftService <-->|GPIO/Serial| LiftHardware
    LiftHardware -->|Arrival Notification| LiftService
```

