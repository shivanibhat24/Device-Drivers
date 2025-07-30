import pyb

def main():
    green_led=pyb.Pin('PA5',mode=pyb.Pin.OUT_PP)
    button=pyb.Pin('PC13',mode=pyb.Pin.IN, pull=pyb.Pin.PULL_DOWN)
    while True:
        if(button.value()==0):
            green_led.value(1)
        else:
            green_led.value(0)

main()