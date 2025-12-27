# Lift Control System - Sequence Diagram

## Main Flow: Sending Floor Command

```mermaid
sequenceDiagram
    participant User/Robot
    participant ClientPi as Client Pi<br/>(Command Sender)
    participant LiftService as Lift Control Service<br/>(Flask API)
    participant LiftHardware as Lift Hardware<br/>(Physical Mechanism)

    Note over User/Robot,LiftHardware: Initialization & Health Check
    User/Robot->>ClientPi: Request to send floor command
    ClientPi->>LiftService: GET /health
    LiftService-->>ClientPi: {status: "healthy", timestamp}
    
    Note over User/Robot,LiftHardware: Floor Command Flow
    ClientPi->>LiftService: POST /api/floor<br/>{floor: 5}
    LiftService->>LiftService: Validate floor number
    LiftService->>LiftService: Update lift_state<br/>(target_floor, status)
    LiftService-->>ClientPi: {status: "success",<br/>target_floor: 5,<br/>lift_status: "moving_up"}
    ClientPi-->>User/Robot: Command acknowledged
    
    Note over User/Robot,LiftHardware: Status Monitoring
    User/Robot->>ClientPi: Check lift status
    ClientPi->>LiftService: GET /api/status
    LiftService-->>ClientPi: {current_floor: 0,<br/>target_floor: 5,<br/>status: "moving_up"}
    ClientPi-->>User/Robot: Status update
    
    Note over User/Robot,LiftHardware: Lift Movement & Arrival
    LiftHardware->>LiftHardware: Move lift to floor 5
    LiftHardware->>LiftService: POST /api/arrived<br/>{floor: 5}
    LiftService->>LiftService: Update lift_state<br/>(current_floor: 5,<br/>status: "arrived")
    LiftService-->>LiftHardware: {status: "success",<br/>current_floor: 5}
    
    Note over User/Robot,LiftHardware: Final Status Check
    User/Robot->>ClientPi: Check if arrived
    ClientPi->>LiftService: GET /api/status
    LiftService-->>ClientPi: {current_floor: 5,<br/>target_floor: null,<br/>status: "arrived"}
    ClientPi-->>User/Robot: Lift arrived at floor 5
```

## Error Handling Flow

```mermaid
sequenceDiagram
    participant ClientPi as Client Pi
    participant LiftService as Lift Control Service

    Note over ClientPi,LiftService: Invalid Floor Command
    ClientPi->>LiftService: POST /api/floor<br/>{floor: "invalid"}
    LiftService->>LiftService: Validate floor (fails)
    LiftService-->>ClientPi: 400 {error: "Floor must be an integer"}
    
    Note over ClientPi,LiftService: Missing Floor Field
    ClientPi->>LiftService: POST /api/floor<br/>{}
    LiftService->>LiftService: Check for floor field (missing)
    LiftService-->>ClientPi: 400 {error: "Missing 'floor' field"}
    
    Note over ClientPi,LiftService: Service Unavailable
    ClientPi->>LiftService: POST /api/floor<br/>{floor: 3}
    LiftService-->>ClientPi: Connection timeout/error
    ClientPi->>ClientPi: Handle error, retry or notify user
```

## Complete System Architecture

```mermaid
sequenceDiagram
    participant User as User/Robot
    participant ClientPi as Client Pi<br/>(Raspberry Pi #1)
    participant Network as Network<br/>(WiFi/Ethernet)
    participant LiftPi as Lift Control Pi<br/>(Raspberry Pi #2)
    participant LiftService as Flask API<br/>(Port 5000)
    participant LiftHardware as Lift Hardware<br/>(In Lift Panel)

    Note over User,LiftHardware: System Startup
    LiftPi->>LiftService: Start Flask service<br/>(host: 0.0.0.0, port: 5000)
    LiftService->>LiftService: Initialize lift_state<br/>(current_floor: 0, status: idle)
    
    Note over User,LiftHardware: Command Request Flow
    User->>ClientPi: "Go to floor 5"
    ClientPi->>Network: HTTP POST /api/floor<br/>{floor: 5}
    Network->>LiftPi: Forward request
    LiftPi->>LiftService: Process floor command
    LiftService->>LiftService: Validate & update state
    LiftService->>Network: HTTP 200 Response
    Network->>ClientPi: Return success
    ClientPi->>User: "Command accepted"
    
    Note over User,LiftHardware: Physical Movement
    LiftService->>LiftHardware: Signal to move<br/>(via GPIO/Serial/etc)
    LiftHardware->>LiftHardware: Move lift to floor 5
    LiftHardware->>LiftService: POST /api/arrived<br/>{floor: 5}
    LiftService->>LiftService: Update current_floor = 5
    LiftService->>LiftHardware: Acknowledge arrival
    
    Note over User,LiftHardware: Status Query
    User->>ClientPi: "What's the status?"
    ClientPi->>Network: HTTP GET /api/status
    Network->>LiftPi: Forward request
    LiftPi->>LiftService: Get current state
    LiftService->>Network: Return status JSON
    Network->>ClientPi: Status response
    ClientPi->>User: "Lift at floor 5, idle"
```

## Component Interaction Overview

```mermaid
graph TB
    subgraph "External"
        User[User/Robot]
    end
    
    subgraph "Client Side"
        ClientPi[Client Pi<br/>Raspberry Pi #1<br/>Runs client_example.py]
    end
    
    subgraph "Network"
        Network[WiFi/Ethernet<br/>TCP/IP]
    end
    
    subgraph "Lift Control Side"
        LiftPi[Lift Control Pi<br/>Raspberry Pi #2<br/>In Lift Panel]
        LiftService[Flask API Service<br/>app.py<br/>Port 5000]
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

