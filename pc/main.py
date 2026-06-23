import json
import socket
import time

import cv2


DEADZONE_X = 40
DEADZONE_Y = 30
SMOOTHING_ALPHA = 0.3
MAX_MISSED_FRAMES = 5

ESP32_IP = "192.168.1.12"
ESP32_PORT = 5000

# Send at 10 Hz instead of once per camera frame.
COMMAND_INTERVAL_SECONDS = 0.1


class ESP32Client:
    def __init__(self, host, port):
        self.host = host
        self.port = port
        self.socket = None
        self.last_connection_attempt = 0

    def connect(self):
        # Avoid attempting to reconnect every camera frame.
        if time.monotonic() - self.last_connection_attempt < 2:
            return False

        self.last_connection_attempt = time.monotonic()

        try:
            self.socket = socket.create_connection(
                (self.host, self.port),
                timeout=0.5,
            )

            print(f"Connected to ESP32 at {self.host}:{self.port}")
            return True

        except OSError:
            self.socket = None
            return False

    def send_command(self, command):
        if self.socket is None and not self.connect():
            return False

        message = json.dumps(command) + "\n"

        try:
            self.socket.sendall(message.encode("utf-8"))
            return True

        except OSError:
            print("ESP32 connection lost.")
            self.close()
            return False

    def close(self):
        if self.socket is not None:
            self.socket.close()
            self.socket = None


def get_face_area(face):
    x, y, width, height = face
    return width * height


def get_pan_command(error_x):
    if error_x < -DEADZONE_X:
        return "LEFT"

    if error_x > DEADZONE_X:
        return "RIGHT"

    return "STOP"


def get_tilt_command(error_y):
    if error_y < -DEADZONE_Y:
        return "UP"

    if error_y > DEADZONE_Y:
        return "DOWN"

    return "STOP"


def main():
    camera = cv2.VideoCapture(0)

    face_cascade = cv2.CascadeClassifier(
        cv2.data.haarcascades
        + "haarcascade_frontalface_default.xml"
    )

    if not camera.isOpened():
        print("Could not open the camera.")
        print("Close other apps using it or try another camera index.")
        return

    if face_cascade.empty():
        print("Could not load the face detector.")
        camera.release()
        return

    esp32 = ESP32Client(ESP32_IP, ESP32_PORT)

    print("Camera opened. Face detection is running.")
    print("Press q in the video window to quit.")

    smoothed_error_x = None
    smoothed_error_y = None

    command_error_x = 0
    command_error_y = 0

    pan_command = "STOP"
    tilt_command = "STOP"

    missed_frames = 0
    sequence_number = 0
    last_command_time = 0

    while True:
        frame_read, frame = camera.read()

        if not frame_read:
            print("Could not read a frame from the camera.")
            break

        gray_frame = cv2.cvtColor(
            frame,
            cv2.COLOR_BGR2GRAY,
        )

        faces = face_cascade.detectMultiScale(
            gray_frame,
            scaleFactor=1.1,
            minNeighbors=5,
            minSize=(60, 60),
        )

        frame_height, frame_width = frame.shape[:2]
        frame_center_x = frame_width // 2
        frame_center_y = frame_height // 2

        # Draw the center of the frame.
        cv2.circle(
            frame,
            (frame_center_x, frame_center_y),
            6,
            (255, 0, 0),
            -1,
        )

        # Draw the deadzone.
        deadzone_top_left = (
            frame_center_x - DEADZONE_X,
            frame_center_y - DEADZONE_Y,
        )

        deadzone_bottom_right = (
            frame_center_x + DEADZONE_X,
            frame_center_y + DEADZONE_Y,
        )

        cv2.rectangle(
            frame,
            deadzone_top_left,
            deadzone_bottom_right,
            (255, 255, 0),
            2,
        )

        if len(faces) > 0:
            largest_face = max(faces, key=get_face_area)
            x, y, width, height = largest_face

            cv2.rectangle(
                frame,
                (x, y),
                (x + width, y + height),
                (0, 255, 0),
                2,
            )

            face_center_x = x + width // 2
            face_center_y = y + height // 2

            cv2.circle(
                frame,
                (face_center_x, face_center_y),
                6,
                (0, 0, 255),
                -1,
            )

            error_x = face_center_x - frame_center_x
            error_y = face_center_y - frame_center_y

            if smoothed_error_x is None:
                smoothed_error_x = error_x
                smoothed_error_y = error_y
            else:
                smoothed_error_x = (
                    SMOOTHING_ALPHA * error_x
                    + (1 - SMOOTHING_ALPHA)
                    * smoothed_error_x
                )

                smoothed_error_y = (
                    SMOOTHING_ALPHA * error_y
                    + (1 - SMOOTHING_ALPHA)
                    * smoothed_error_y
                )

            smoothed_error_x = round(smoothed_error_x)
            smoothed_error_y = round(smoothed_error_y)

            command_error_x = smoothed_error_x
            command_error_y = smoothed_error_y

            missed_frames = 0

            pan_command = get_pan_command(smoothed_error_x)
            tilt_command = get_tilt_command(smoothed_error_y)

            error_text = (
                f"Smoothed X: {smoothed_error_x}  "
                f"Smoothed Y: {smoothed_error_y}"
            )

            command_text = (
                f"Pan: {pan_command}  "
                f"Tilt: {tilt_command}"
            )

            cv2.putText(
                frame,
                error_text,
                (10, 30),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.7,
                (0, 255, 0),
                2,
            )

            cv2.putText(
                frame,
                command_text,
                (10, 60),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.7,
                (0, 255, 0),
                2,
            )

        else:
            missed_frames += 1

            if missed_frames <= MAX_MISSED_FRAMES:
                status_text = (
                    f"Face temporarily lost "
                    f"({missed_frames}/{MAX_MISSED_FRAMES})  "
                    f"Pan: {pan_command}  "
                    f"Tilt: {tilt_command}"
                )

            else:
                pan_command = "STOP"
                tilt_command = "STOP"

                command_error_x = 0
                command_error_y = 0

                smoothed_error_x = None
                smoothed_error_y = None

                status_text = (
                    "No face - Pan: STOP  Tilt: STOP"
                )

            cv2.putText(
                frame,
                status_text,
                (10, 30),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.6,
                (0, 255, 255),
                2,
            )

        # Send the latest command to the ESP32 at 10 Hz.
        current_time = time.monotonic()

        if (
            current_time - last_command_time
            >= COMMAND_INTERVAL_SECONDS
        ):
            command = {
                "seq": sequence_number,
                "pan": pan_command,
                "tilt": tilt_command,
                "error_x": command_error_x,
                "error_y": command_error_y,
            }

            esp32.send_command(command)

            sequence_number += 1
            last_command_time = current_time

        cv2.imshow("Face Detection", frame)

        if cv2.waitKey(1) & 0xFF == ord("q"):
            break

    esp32.close()
    camera.release()
    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()