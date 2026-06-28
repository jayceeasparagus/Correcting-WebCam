import argparse
import json
import socket
import time

import cv2


DEADZONE_X = 40
DEADZONE_Y = 30

# Lower = smoother/slower response, higher = quicker/more jittery.
SMOOTHING_ALPHA = 0.22

# Limits one-frame jumps before the EMA filter sees them.
MAX_ERROR_DELTA_X = 45
MAX_ERROR_DELTA_Y = 35

# Prevents command chatter near the deadzone edge.
COMMAND_HYSTERESIS_X = 12
COMMAND_HYSTERESIS_Y = 10

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
            self.socket.settimeout(0.01)

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
            self.read_available_responses()
            return True

        except OSError:
            print("ESP32 connection lost.")
            self.close()
            return False

    def close(self):
        if self.socket is not None:
            self.socket.close()
            self.socket = None

    def read_available_responses(self):
        if self.socket is None:
            return

        try:
            data = self.socket.recv(1024)
        except socket.timeout:
            return
        except OSError:
            self.close()
            return

        if data:
            for line in data.decode("utf-8", errors="replace").splitlines():
                print(f"ESP32: {line}")


class ErrorSmoother:
    def __init__(self, alpha, max_delta_x, max_delta_y):
        self.alpha = alpha
        self.max_delta_x = max_delta_x
        self.max_delta_y = max_delta_y
        self.error_x = None
        self.error_y = None

    def reset(self):
        self.error_x = None
        self.error_y = None

    def update(self, raw_x, raw_y):
        if self.error_x is None:
            self.error_x = raw_x
            self.error_y = raw_y
            return round(self.error_x), round(self.error_y)

        limited_x = self._limit_jump(raw_x, self.error_x, self.max_delta_x)
        limited_y = self._limit_jump(raw_y, self.error_y, self.max_delta_y)

        self.error_x = (
            self.alpha * limited_x
            + (1 - self.alpha) * self.error_x
        )
        self.error_y = (
            self.alpha * limited_y
            + (1 - self.alpha) * self.error_y
        )

        return round(self.error_x), round(self.error_y)

    @staticmethod
    def _limit_jump(raw_value, previous_value, max_delta):
        delta = raw_value - previous_value

        if delta > max_delta:
            return previous_value + max_delta

        if delta < -max_delta:
            return previous_value - max_delta

        return raw_value


def get_face_area(face):
    x, y, width, height = face
    return width * height


def get_pan_command(error_x, previous_command):
    return get_axis_command(
        error=error_x,
        previous_command=previous_command,
        negative_command="LEFT",
        positive_command="RIGHT",
        deadzone=DEADZONE_X,
        hysteresis=COMMAND_HYSTERESIS_X,
    )


def get_tilt_command(error_y, previous_command):
    return get_axis_command(
        error=error_y,
        previous_command=previous_command,
        negative_command="UP",
        positive_command="DOWN",
        deadzone=DEADZONE_Y,
        hysteresis=COMMAND_HYSTERESIS_Y,
    )


def get_axis_command(
    error,
    previous_command,
    negative_command,
    positive_command,
    deadzone,
    hysteresis,
):
    enter_threshold = deadzone + hysteresis
    exit_threshold = max(0, deadzone - hysteresis)

    if previous_command == negative_command and error < -exit_threshold:
        return negative_command

    if previous_command == positive_command and error > exit_threshold:
        return positive_command

    if error < -enter_threshold:
        return negative_command

    if error > enter_threshold:
        return positive_command

    return "STOP"


def parse_args():
    parser = argparse.ArgumentParser(
        description="OpenCV webcam tracker that sends pan/tilt commands to ESP32."
    )
    parser.add_argument(
        "--esp32-ip",
        default=ESP32_IP,
        help="ESP32 IP address printed in the ESP32 Serial Monitor.",
    )
    parser.add_argument(
        "--esp32-port",
        type=int,
        default=ESP32_PORT,
        help="ESP32 TCP server port.",
    )
    parser.add_argument(
        "--camera",
        type=int,
        default=0,
        help="OpenCV camera index.",
    )
    return parser.parse_args()


def main():
    args = parse_args()

    camera = cv2.VideoCapture(args.camera)

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

    esp32 = ESP32Client(args.esp32_ip, args.esp32_port)
    smoother = ErrorSmoother(
        alpha=SMOOTHING_ALPHA,
        max_delta_x=MAX_ERROR_DELTA_X,
        max_delta_y=MAX_ERROR_DELTA_Y,
    )

    print("Camera opened. Haar face detection is running.")
    print("Press q in the video window to quit.")

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

        cv2.circle(
            frame,
            (frame_center_x, frame_center_y),
            6,
            (255, 0, 0),
            -1,
        )

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

            smoothed_error_x, smoothed_error_y = smoother.update(
                error_x,
                error_y,
            )

            command_error_x = smoothed_error_x
            command_error_y = smoothed_error_y

            missed_frames = 0

            pan_command = get_pan_command(
                smoothed_error_x,
                pan_command,
            )
            tilt_command = get_tilt_command(
                smoothed_error_y,
                tilt_command,
            )

            error_text = (
                f"Raw X: {error_x}  Raw Y: {error_y}  "
                f"Smooth X: {smoothed_error_x}  "
                f"Smooth Y: {smoothed_error_y}"
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
                0.55,
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

                smoother.reset()

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

            command_sent = esp32.send_command(command)

            if command_sent:
                print(
                    f"Sent seq={sequence_number} "
                    f"pan={pan_command} tilt={tilt_command} "
                    f"error_x={command_error_x} error_y={command_error_y}"
                )

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
