# Lift Control System - Block & Sequence Diagram

## Main Flow Sequence Diagram

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
    Note over RobotMainController,CameraModule: Robot & Camera work together<br/>to position before lift
    RobotMainController->>CameraModule: Position before lift<br/>(via ROS or REST ?)
    activate CameraModule
    CameraModule->>CameraModule: Detect robot position<br/>& go to right position in front of lift
    CameraModule-->>RobotMainController: Positioned
    deactivate CameraModule
    RobotMainController-->>ClientPi: Positioned
    ClientPi->>LiftService: Go to Floor 3, pick up and go to Floor 5<br/>(via LoRa)
    LiftService-->>ClientPi: Command acknowledged
    
    Note over User,LiftHardware: Lift Movement & Arrival
    LiftService->>LiftHardware: Instruct thru Mechanical hand
    LiftHardware->>LiftHardware: Move lift to floor 3
    LiftHardware->>LiftService: POST /api/arrived<br/>{floor: 5}
    LiftService->>LiftService: Update lift_state<br/>(current_floor: 5,<br/>status: "arrived")
    LiftService-->>LiftHardware: {status: "success",<br/>current_floor: 5}
    
    Note over User,LiftHardware: Robot's Status Monitoring
    CameraModule->>CameraModule: Check if the Lift door is opened

    Note over User,LiftHardware: Final Status Check
    User->>ClientPi: Check if arrived
    ClientPi->>LiftService: GET /api/status<br/>(via LoRa)
    LiftService-->>ClientPi: {current_floor: 5,<br/>target_floor: null,<br/>status: "arrived"}<br/>(via LoRa)
    ClientPi-->>User: Lift arrived at floor 5
```

## Component Interaction Overview - Block Diagram

```mermaid
graph TB
    subgraph "External"
        User[User]
    end
    
    subgraph "Robot Side"
        RobotRMR[Robot RMR - ESP<br/>WiFi router & LoRa device<br/>Port 5000]
        RobotMainController[Robot Main Controller<br/>Polls for commands]
        RobotCamera[Robot Camera<br/>Raspberry Pi<br/>Port 5001]
    end
    
    subgraph "Lift Control Side"
        PanelRMR[Panel RMR - ESP<br/>app.py<br/>Port 5000]
        LiftHardware[Lift Hardware<br/>Physical Mechanism<br/>Motors, Sensors]
    end
    
    User -->|Commands| RobotRMR
    RobotMainController <-->|WiFi<br/>GET /api/pending-command| RobotRMR
    RobotMainController <-->|WiFi<br/>POST /api/positioned| RobotRMR
    RobotMainController <-->|ROS or REST<br/>Position commands| RobotCamera
    RobotRMR <-->|LoRa<br/>Floor commands| PanelRMR
    PanelRMR <-->|Mechanical hand<br/>GPIO/Serial| LiftHardware
    LiftHardware -->|Arrival Notification| PanelRMR
```

