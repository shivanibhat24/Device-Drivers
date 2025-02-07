import pyb

def main():
    green_led = pyb.Pin('PA5', mode=pyb.Pin.OUT_PP)
    print(green_led.af_list())
    print(green_led.port())
    print(green_led_name())
    print(green_led_value())
    while True:
        green_led_value(1)
        pyb.delay(1000)
        green_led_value(0)
        pyb.delay(1000)

main()