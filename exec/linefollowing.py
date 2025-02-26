import cv2
import numpy as np
from picamera2 import Picamera2
import time
import threading
import sys
from time import sleep
import RPi.GPIO as GPIO
import os
import re

deltocarpipe_path = 'deltocar_fifo'
tablepipe_path = ['table1_fifo', 'table2_fifo', 'table3_fifo', 'table4_fifo']

#pin
LEFT_MOTOR_FORWARD = 18
LEFT_MOTOR_BACKWARD = 16
RIGHT_MOTOR_FORWARD = 31
RIGHT_MOTOR_BACKWARD = 29
PIN_LED35=35
PIN_LED36=36
PIN_LED37=37
PIN_LED38=38

GPIO.setwarnings(False)
GPIO.setmode(GPIO.BOARD)
GPIO.setup(LEFT_MOTOR_FORWARD, GPIO.OUT)
GPIO.setup(LEFT_MOTOR_BACKWARD, GPIO.OUT)
GPIO.setup(RIGHT_MOTOR_FORWARD, GPIO.OUT)
GPIO.setup(RIGHT_MOTOR_BACKWARD, GPIO.OUT)
GPIO.setup(PIN_LED35, GPIO.OUT)
GPIO.setup(PIN_LED36, GPIO.OUT)
GPIO.setup(PIN_LED37, GPIO.OUT)
GPIO.setup(PIN_LED38, GPIO.OUT)

left_DutyA = GPIO.PWM(LEFT_MOTOR_FORWARD, 100)
right_DutyA = GPIO.PWM(RIGHT_MOTOR_FORWARD, 100)
left_DutyB = GPIO.PWM(LEFT_MOTOR_BACKWARD,100)
right_DutyB = GPIO.PWM(RIGHT_MOTOR_BACKWARD,100)

def move_forward():
    left_DutyB.start(0)
    right_DutyB.start(0)
    left_DutyA.start(12)
    right_DutyA.start(12)

def turn_left():
    left_DutyB.start(0)
    right_DutyB.start(0)
    left_DutyA.start(0)
    right_DutyA.start(13)

def turn_right():
    left_DutyB.start(0)
    right_DutyB.start(0)
    left_DutyA.start(13)
    right_DutyA.start(0)

def stop():
    left_DutyA.stop()
    left_DutyB.stop()
    right_DutyA.stop()
    right_DutyB.stop()
    
picam2 = Picamera2()
config = picam2.create_still_configuration(main={'size': (640, 480)})
picam2.configure(config)
picam2.start()

lower_green = np.array([40, 50, 60])
upper_green = np.array([90, 255, 255])
lower_red = np.array([110,100,50])
upper_red = np.array([140,255,255])
lower_blue = np.array([0, 120, 70])
upper_blue = np.array([10, 255, 255])
lower_white=np.array([0,0,200])
upper_white=np.array([180,25,255])
lower_yellow = np.array([20, 100, 100])
upper_yellow = np.array([30, 255, 255])

def timer(seconds):
    global flag
    time.sleep(seconds)
    flag=1

table=[0,0,0,0,0,0]
done_meal=[0,0,0,0,0,0,0,0,0,0,0,0]

if not os.path.exists(deltocarpipe_path):
    print(f"Pipe {deltocarpipe_path} does not exist!")
    exit(1)
for i in range(4):
    if not os.path.exists(tablepipe_path[i]):
        print(f"Pipe {tablepipe_path[i]} does not exist!")
        exit(1)
    
with open(deltocarpipe_path, 'r') as deltocarpipe, open(tablepipe_path[0], 'w') as tablepipe1, open(tablepipe_path[1], 'w') as tablepipe2, open(tablepipe_path[2], 'w') as tablepipe3, open(tablepipe_path[3], 'w') as tablepipe4:

    while True:
        table=[0,0,0,0,0,0]
        for i in range(1,5):
            GPIO.output(34+i,GPIO.LOW)
        data=deltocarpipe.read(24)
        if len(data)==0:
            print("writer closed")
            break
        print('Read: "{0}"'.format(data))
        data_cleaned=re.sub(r'[^\d\s]', '', data) #just number and space left
        split_data=data_cleaned.split() #change data(12) into 4*(3)
        result=[f"{split_data[i]} {split_data[i+1]} {split_data[i+2]}" for i in range(0, len(split_data), 3)]
        print(result)
        
        station_count=0
        run_outside=0
        flag=0
        timer_thread=threading.Thread(target=timer,args=(2,))
        timer_thread.start()
        #收到23個char，轉換成12個數字存在done_meal[]
        for i in range(12):
            done_meal[i]=data[2*i]
        #三個數字，其中一個大於零，代表該桌子要送，table[]=1
        for i in range(3):
            print(done_meal[i],done_meal[i+3],done_meal[i+6],done_meal[i+9])
            if int(done_meal[i])>0: 
                table[1]=1
            if int(done_meal[i+3])>0:
                table[2]=1
            if int(done_meal[i+6])>0:
                table[3]=1
            if int(done_meal[i+9])>0:
                table[4]=1

        for i in range(1,5):
            if(table[i]==1):
                GPIO.output(34+i,GPIO.HIGH)
        
        #後面兩桌是1桌跟2桌，如果1、2桌都沒有東西要送，那就走小圈的(run_outside=0)，反之走大圈
        if table[2]!=0 or table[3]!=0:
            run_outside=1


        while True:
            im = picam2.capture_array()
            hsv = cv2.cvtColor(im, cv2.COLOR_BGR2HSV)
            mask_green= cv2.inRange(hsv, lower_green, upper_green)
            mask_red=cv2.inRange(hsv, lower_red, upper_red)
            mask_white=cv2.inRange(hsv, lower_white,upper_white)
            mask_blue=cv2.inRange(hsv,lower_blue,upper_blue)
            mask_yellow=cv2.inRange(hsv,lower_yellow,upper_yellow)

            #masked_frame = cv2.bitwise_and(im, im, mask=mask_white)

            right_mask_green=mask_green[:, 570:]
            left_mask_green=mask_green[:, :570]
            right_mask_red=mask_red[:, 570:]
            left_mask_red=mask_red[:, :570]
            right_mask_yellow=mask_yellow[:, 570:]
            left_mask_yellow=mask_yellow[:, :570]

            right_green_area=cv2.countNonZero(right_mask_green)
            left_green_area=cv2.countNonZero(left_mask_green)
            right_red_area=cv2.countNonZero(right_mask_red)
            left_red_area=cv2.countNonZero(left_mask_red)
            blue_area=cv2.countNonZero(mask_blue)
            right_yellow_area=cv2.countNonZero(right_mask_yellow)
            left_yellow_area=cv2.countNonZero(left_mask_yellow)

            #跑的時候不要開鏡頭，不然delay有時候會有bug
            #cv2.imshow("preview",masked_frame)

            #如果是跑內圈
            if run_outside==0:   #inside
                if(blue_area>0 and flag):#藍色是station，，flag是為了不要讓藍色一直偵測到，所以用timer
                    station_count+=1
                    print(station_count)
                    inside_table=[0,table[1],table[4]]
                    flag=0
                    timer_thread=threading.Thread(target=timer,args=(2,))
                    timer_thread.start()
                    if station_count==3:#到達廚房直接停下來等待下一筆餐點送出
                        stop()
                        sleep(2)
                        break
                    if(inside_table[station_count]==1):#偵測到藍色而且該桌有餐點要送的話會停下來
                        #如果station_count=1那就是第1桌，如果station_count=2，那就是第4桌
                        if(station_count==1):
                            GPIO.output(PIN_LED35,GPIO.LOW)
                            tablepipe1.write(result[0])
                            tablepipe1.flush()
                        elif(station_count==2):
                            GPIO.output(PIN_LED38,GPIO.LOW)
                            tablepipe4.write(result[3])
                            tablepipe4.flush()
                        stop()
                        sleep(3)
                        flag=0
                        timer_thread=threading.Thread(target=timer,args=(2,))
                        timer_thread.start()
                    
                if right_red_area>0:
                    turn_left()
                elif left_red_area>0:
                    turn_right() 
                else:
                    if right_green_area > 0:
                        turn_left()
                    elif left_green_area> 0:
                        turn_right()
                    else:
                        move_forward()
            #如果跑外圈
            else:
                if(blue_area>0 and flag):
                    flag=0
                    timer_thread=threading.Thread(target=timer,args=(2,))
                    timer_thread.start()
                    station_count+=1
                    print(station_count)
                    if station_count==5:#=5代表到廚房了
                        stop()
                        sleep(2)
                        break
                    if(table[station_count]==1):#偵測到藍色而且該桌有餐點要送的話會停下來
                        #station_count=1代表第0桌，=2代表第1桌，=3代表第二桌，=4代表第三桌
                        if(station_count==1):
                            GPIO.output(PIN_LED35,GPIO.LOW)
                            tablepipe1.write(result[0])
                            tablepipe1.flush()
                        elif(station_count==2):
                            GPIO.output(PIN_LED36,GPIO.LOW)
                            tablepipe2.write(result[1])
                            tablepipe2.flush()
                        elif(station_count==3):
                            GPIO.output(PIN_LED37,GPIO.LOW)
                            tablepipe3.write(result[2])
                            tablepipe3.flush()
                        elif(station_count==4):
                            GPIO.output(PIN_LED38,GPIO.LOW)
                            tablepipe4.write(result[3])
                            tablepipe4.flush()
                        stop()
                        sleep(3)
                        flag=0
                        timer_thread=threading.Thread(target=timer,args=(2,))
                        timer_thread.start()
                    
                        
                if right_green_area > 0 or right_yellow_area>0:
                    turn_left()
                elif left_green_area> 0 or left_yellow_area>0:
                    turn_right()
                else:
                    move_forward()

                        
            if cv2.waitKey(1)==ord('q'):
                break


picam2.stop()
GPIO.cleanup()
cv2.destroyAllWindows()
