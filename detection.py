import cv2
import numpy as np
import time
import os
import signal
import sys
import logging
from pupil_apriltags import Detector
import math
import time

# ===== Config =====
camera_id = 8
tag_size = 0.08  # meters (make sure this matches your actual tag size)
calib_file = "/home/apriltagtester01/rmr-tagdetection/test_find_tags/calibration_savez.npz"
led_status_file = "/home/apriltagtester01/rmr-tagdetection/test_find_tags/led_status.txt"
x_status_file = "/home/apriltagtester01/rmr-tagdetection/test_find_tags/x_status"
z_status_file = "/home/apriltagtester01/rmr-tagdetection/test_find_tags/z_status"
robot_mode = "/home/apriltagtester01/rmr-tagdetection/test_find_tags/robot_mode"

# ===== Logging Setup =====
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] %(message)s',
    handlers=[
        logging.StreamHandler(sys.stdout)
    ]
)

# ===== Load Calibration =====
if os.path.exists(calib_file):
    with np.load(calib_file) as data:
        camera_matrix = data['mtx']
        dist_coeffs = data['dist']
    logging.warning("Calibration file all good")
    print("Calibration file all good")
else:
    logging.warning("Calibration file not found, using default matrix.")
    camera_matrix = np.array([[600, 0, 320],
                              [0, 600, 240],
                              [0, 0, 1]])
    print("Calibration file not found")

fx, fy = camera_matrix[0, 0], camera_matrix[1, 1]
cx, cy = camera_matrix[0, 2], camera_matrix[1, 2]

# ===== Initialize AprilTag Detector =====
detector36 = Detector(
    families='tag36h11',
    nthreads=1,
    quad_decimate=1.0,
    quad_sigma=0.0,
    refine_edges=1,
    decode_sharpening=1.0,
    debug=0
)

detector25 = Detector(
    families='tag25h9',
    nthreads=1,
    quad_decimate=1.0,
    quad_sigma=0.0,
    refine_edges=1,
    decode_sharpening=1.0,
    debug=0
)

# ===== Open Camera =====
cap = cv2.VideoCapture(camera_id)
if not cap.isOpened():
    logging.error("Cannot open camera " + str(camera_id))
    print("Failed to open /dev/video8")
    sys.exit(1)
else:
    print("Opened /dev/video8")

# ====== Debounce variables =====
close_count = 0
far_count = 0
threshold_count = 3  # Number of consecutive frames needed to switch LED state
led_state = False  # False=OFF, True=ON
distance_status = False


def write_led_status(state: bool):
    try:
        with open(led_status_file, 'w') as f:
            f.write("ON" if state else "OFF")

    except Exception as e:
        logging.error("Failed to write LED status: " + str(e))


def write_x_status(state: str):
    try:
        with open(x_status_file, 'w') as f:
            f.write(state)

    except Exception as e:
        logging.error("something's wrong (x): " + str(e))


def write_z_status(state: str):
    try:
        with open(z_status_file, 'w') as f:
            f.write(state)

    except Exception as e:
        logging.error("something's wrong (z): " + str(e))
        
def read_mode():
    try:
        with open(robot_mode, 'r') as f:
            return f.read().strip()

    except Exception as e:
        logging.error("something's wrong (mode): " + str(e))


def cleanup(signal_received=None, frame=None):
    cap.release()
    write_led_status(False)
    logging.info("Detection stopped. LED OFF.")
    sys.exit(0)


# ===== Register Signal Handlers =====
signal.signal(signal.SIGINT, cleanup)
signal.signal(signal.SIGTERM, cleanup)

# ===== Init =====
write_led_status(False)
logging.info("Detection started (headless mode).")

def rounddown(n):
    return math.floor(n*10)/10

# ===== Main Loop =====
count = 0
x_total = 0
z_total = 0
x_average = 0
z_average = 0
z = 0
x = 0

# === Detection Mechanism ====

def tag_detection():
    global x_total, z_total, count, z_average, z, x
    x = float(det.pose_t[0][0])
    z = float(det.pose_t[2][0])
    x_total += x
    z_total += z
    #dist_status = 0
    count+= 1
    if count == 5:
        x_average = rounddown(x_total/5 * 100)
        write_x_status(str(x_average))
        logging.info(str(x_average))
        z_average = rounddown(z_total/5 * 100)
        #write_z_status(str(z_average))
        #logging.info(str(z_average))
        dist_status = z_average - 50
    if count > 5:
        x_total = 0
        z_total = 0
        count = 0

try:
    while True:
        ret, frame = cap.read()
        if not ret:
            logging.warning("Failed to grab frame")
            continue

        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        
        detections36 = detector36.detect(
            gray,
            estimate_tag_pose=True,
            camera_params=(fx, fy, cx, cy),
            tag_size=tag_size
        )

        detections25 = detector25.detect(
            gray,
            estimate_tag_pose=True,
            camera_params=(fx, fy, cx, cy),
            tag_size=tag_size
        )

        tag_detected_close = any(
            det.pose_t is not None and det.pose_t[2][0] < 4.0
            for det in detections36 or detections25
        )
        
        mode = read_mode()

        # Debounce logic for LED state file
        if tag_detected_close:
            close_count += 1
            far_count = 0
        else:
            far_count += 1
            close_count = 0

        if close_count >= threshold_count and not led_state:
            led_state = True
            write_led_status(True)
            logging.info("LED STATUS: ON (Elevator entered)")

        if far_count >= threshold_count and led_state:
            led_state = False
            write_led_status(False)
            logging.info("LED STATUS: OFF (Elevator exited)")

            # tag_detected_close = False
        for det in detections36:
            print(det.tag_family)
            if det.pose_t is not None:
                if mode == "ENTRY":
                    tag_detection()
                    if z >= 0.50:
                        write_z_status(str(z_average - 50))
                        logging.info(str(z_average - 50))
                    if z < 0.50:
                        write_z_status("entered1")
                        logging.info("entered1")
                if mode == "EXIT":
                    tag_detection()
                    if z < 1:
                        write_z_status(str(z_average))
                        logging.info(str(z_average))
                    if z >= 1:
                        write_z_status("exited")
                        logging.info("exited")

        for det in detections25:
            print(det.tag_family)
            if det.pose_t is not None:
                tag_detection()
                if z >= 0.50:
                    write_z_status(str(z_average))
                    logging.info(str(z_average))
                if z < 0.50:
                    write_z_status("entered2")
                    logging.info("entered2")

        time.sleep(0.0000001)
except Exception as e:
    logging.error("Unexpected error: " + str(e))
    cleanup()
