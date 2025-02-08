import logging
from pynput.mouse import Listener

logging.basicConfig(filename='coords.log', level=logging.INFO, format='%(asctime)s - %(message)s')

button_pressed = False

def on_click(x, y, button, pressed):
    global button_pressed
    # Log only when the mouse is pressed and released (full click)
    if pressed and not button_pressed:  # Mouse button just pressed
        button_pressed = True  # Mark the button as pressed
    elif not pressed and button_pressed:  # Mouse button just released
        logging.info(f"Mouse clicked at coordinates: ({x}, {y})")  # Log on release
        button_pressed = False  # Reset the pressed state

def monitor_mouse():
    print("Monitoring mouse clicks. Press Ctrl+C to exit.")
    
    with Listener(on_click=on_click) as listener:
        listener.join()

if __name__ == "__main__":
    monitor_mouse()
