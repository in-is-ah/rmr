'''
functions of user interface:
1. user login 
2. robot movement - start, stop 
3. show distance required to move
4. flash RWZ/inside lift with colours 
'''
import tkinter as tk
from tkinter import messagebox
import customtkinter as ctk
import subprocess
from tkinter import filedialog
import sys
import os
import signal
import paho.mqtt.client as mqtt
import time 
import itertools
import logging

logins_txt = '/home/apriltagtester01/rmr-tagdetection/login5'
x_path = '/home/apriltagtester01/rmr-tagdetection/test_find_tags/x_status'
z_path = '/home/apriltagtester01/rmr-tagdetection/test_find_tags/z_status'
det_script = '/home/apriltagtester01/rmr-tagdetection/test_find_tags/run.sh'
robot_mode = '/home/apriltagtester01/rmr-tagdetection/test_find_tags/robot_mode'
#floor_request_file = '/home/apriltagtester01/rmr-tagdetection/test_find_tags/floor_request_file'

root = ctk.CTk()
root.title('Contactless Lift System Movement')
root.geometry('300x500')

login_page_option = ctk.CTkButton(master=root, text='RMR-R v1.0', font=(
    'Bold', 30), width=250, height=40, corner_radius=10, border_width=2,
    fg_color='white', text_color='#3B8ED0', border_color='white', hover=False)
login_page_option.place(x=25, y=10)

page_frame = ctk.CTkFrame(master=root, width=250, height=390, corner_radius=10)
page_frame.place(x=25, y=70)

heading_lb = ctk.CTkLabel(
    master=root, text='IoInspire (National University of Singapore)', font=('Bold', 12))
heading_lb.place(x=25, y=470)

#floor request display
floor_disp_frame = ctk.CTkFrame(master=page_frame, width=200, height=60, corner_radius=10)
floor_disp_frame.place(x=25, y=320)

text_list_floor = tk.Listbox(master=floor_disp_frame, width=20, height=2, font=('Bold', 10))
text_list_floor.place(x=5, y=5)

def read_logins():
    with open(logins_txt, 'r') as f:
        contents = f.readlines()

        new_contents = []

        for line in contents:
            fields = line.split(',')
            fields[1] = fields[1].rstrip()
            new_contents.append(fields)

        return new_contents

logins = read_logins()

def login_page():
    heading_lb_1 = ctk.CTkLabel(
        master=page_frame, text='Login Page', font=('Bold', 30))
    heading_lb_1.place(x=50, y=10)

    user_entry = ctk.CTkEntry(master=page_frame, width=230, height=35,
                              placeholder_text='Username', border_color='#3B8ED0',
                              border_width=2, corner_radius=10)
    user_entry.place(x=10, y=80)

    password_entry = ctk.CTkEntry(master=page_frame, width=230, height=35,
                                  placeholder_text='Password', border_color='#3B8ED0', 
                                  border_width=2, corner_radius=10)
    password_entry.place(x=10, y=150)

    def on_login():
        username = user_entry.get()
        password = password_entry.get()
        logged_in = False

        for line in logins:
            if line[0] == username and not logged_in:
                if line[1] == password:
                    logged_in = True

        if logged_in:
            robot_status_page()
            heading_lb_1.destroy()
            user_entry.destroy()
            password_entry.destroy()
            login_btn.destroy()
            #print_floor_request()

        else:
            messagebox.showerror(title="Login failed",
                                 message="Invalid Username/Password")

    login_btn = ctk.CTkButton(master=root, text='Login', font=(
        'Bold', 20), width=200, height=40, text_color='white', corner_radius=10,
        hover=False, command=on_login)
    login_btn.place(x=50, y=300)


last_x = None
last_z = None
prc = None
prc_pid = 0
        
# set up client-server connection
client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1, "Camera Communication")
mqttBroker = "192.168.4.10"
mqttPort = 1883

floor_status = ""
currentFloor = None
requestedFloor = None

#======robot status page======
def robot_status_page():
    # obtain floor request status
    def floor_req():
        client.on_message = on_message
        client.connect(mqttBroker, mqttPort)
        client.subscribe("robot/floor-request")
        client.loop_start()
        
    #time.sleep(10)
    def on_message(client, userdata, message):
        global floor_status, currentFloor, requestedFloor
        
        topic = message.topic
        payload = str(message.payload.decode("utf-8"))
        
        if topic == "robot/floor-request":
            print("[on_message]: Received floor request: ", payload)
            fields= payload.split(",")
                
            currentFloor = int(fields[0])
            requestedFloor = int(fields[1])
            
            print("Current Floor:", currentFloor)
            print("Requested Floor:", requestedFloor)
            
            #return currentFloor
            print_floor_request()
            entry_mode()
            #start_program()
            
        elif topic == "FLOOR_STATUS":
            floor_status = payload
            print("[on_message]: Received floor status: ", floor_status)
            
    def print_floor_request():
        if currentFloor is None:
            print("yup that's a problem")
        text_list_floor.insert(tk.END, "Current Floor: " + str(currentFloor))
        text_list_floor.insert(tk.END, "Requested Floor: " + str(requestedFloor))
            
    def read_live_mode():
        try:
            with open(z_path) as f:
                return f.read().strip()
        except Exception as e:
            logging.error(f"Error reading {z_path}: {e}")
            return None
            
    def start_program():
        global prc_pid
        if currentFloor is not None:
            prc = subprocess.Popen(["bash", det_script], start_new_session=True,)
            prc_pid = prc.pid
            print("The new process (new session) is started with this pid:", prc_pid)
            #update_value()
            root.after(500, update_value)
            
    def pause_program():
        global prc_pid
        print("The pid that I want to kill is this:", prc_pid)
        os.killpg(prc_pid, signal.SIGKILL)
            
    def entry_mode():
        try:
            with open(robot_mode, 'w') as f:
                f.write("ENTRY")
        except Exception as e:
            logging.error("something's wrong: " + str(e))
        start_program()
                
    def exit_mode():
        try:
            with open(robot_mode, 'w') as f:
                f.write("EXIT")
        except Exception as e:
            logging.error("something's wrong: " + str(e))
        start_program()

    def update_value():
        global last_x
        with open(x_path, 'r') as file:
            new_x = file.readline()
            if last_x != new_x:
                text_list_1.delete(0, tk.END)
                last_x = new_x
                text_list_1.insert(tk.END, new_x)
        global last_z
        with open(z_path, 'r') as file:
            new_z = file.readline()
            
            if last_z != new_z:
                text_list_2.delete(0, tk.END)
                last_z = new_z
                text_list_2.insert(tk.END, new_z)

            if new_z == "entered1":
                arrived()
                client.publish("robot/robot-in", new_z, qos=1)
            elif new_z == "entered2":
                arrived2()
            else:
                not_arrived()
        
                
        root.after(500, update_value)
    
    def arrived():
        update_btn.place(x=15, y=65)
        update2_btn.place_forget()
        x_frame.place_forget()
        z_frame.place_forget()

    def arrived2():
        update2_btn.place(x=15, y=65)
        update_btn.place_forget()
        x_frame.place_forget()
        z_frame.place_forget()

    def not_arrived():
        update_btn.place_forget()
        update2_btn.place_forget()
        x_frame.place(x=15, y=65)
        z_frame.place(x=15, y=200)
        
    floor_req()
    #entry_mode()

    stop_btn = ctk.CTkButton(master=page_frame, width=75, height=35, corner_radius=10,
                              text='STOP', font=('Bold', 15), fg_color='red', command=pause_program, hover=True)
    stop_btn.place(x=165, y=15)

    entry_btn = ctk.CTkButton(master=page_frame, width=75, height=35, corner_radius=10, text='ENTRY', font=(
        'Bold', 15), fg_color='green', command=entry_mode, hover=True)
    entry_btn.place(x=5, y=15)
    
    exit_btn = ctk.CTkButton(master=page_frame, width=75, height=35, corner_radius=10,
                              text='EXIT', font=('Bold', 15), fg_color='green', command=exit_mode, hover=True)
    exit_btn.place(x=85, y=15)

    # x-value updates
    x_frame = ctk.CTkFrame(master=page_frame, width=220,
                           height=120, corner_radius=10)
    x_frame.place(x=15, y=65)

    x_disp_frame = ctk.CTkFrame(
        master=x_frame, width=200, height=70, corner_radius=10)
    x_disp_frame.place(x=10, y=40)

    x_btn = ctk.CTkLabel(
        master=x_frame, text='To move right by (cm):', font=('Bold', 13))
    x_btn.place(x=10, y=10)

    text_list_1 = tk.Listbox(
        master=x_disp_frame, width=4, height=1, font=('Bold', 30))
    text_list_1.place(x=5, y=5)

    # z-value updates
    z_frame = ctk.CTkFrame(master=page_frame, width=220,
                           height=120, corner_radius=10)
    z_frame.place(x=15, y=190)

    z_disp_frame = ctk.CTkFrame(
        master=z_frame, width=200, height=70, corner_radius=10)
    z_disp_frame.place(x=10, y=40)

    z_btn = ctk.CTkLabel(
        master=z_frame, text='To move forward by (cm):', font=('Bold', 13))
    z_btn.place(x=10, y=10)

    text_list_2 = tk.Listbox(
        master=z_disp_frame, width=4, height=1, font=('Bold', 30))
    text_list_2.place(x=5, y=5)

    update_btn = ctk.CTkButton(master=page_frame, width=220, height=200, corner_radius=10,
                               fg_color='yellow', text='at robot\nwaiting zone', font=('Bold', 30), command=update_value, hover=False)

    update2_btn = ctk.CTkButton(master=page_frame, width=220, height=200, corner_radius=10,
                                fg_color='green', text='inside\nlift', font=('Bold', 30), command=update_value, hover=False)


login_page()
root.mainloop()

