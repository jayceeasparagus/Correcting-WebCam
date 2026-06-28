import json
import socket
import time
import argparse
import urllib.request
from pathlib import Path

import cv2


DEADZONE_X = 40
DEADZONE_Y = 30
SMOOTHING_ALPHA = 0.3
MAX_MISSED_FRAMES = 5

ESP32_IP = "192.168.1.12"
ESP32_PORT = 5000

# Send at 10 Hz instead of once per camera frame.
COMMAND_INTERVAL_SECONDS = 0.1

YUNET_MODEL_URL = (
    "https://github.com/opencv/opencv_zoo/raw/main/models/"
    "face_detection_yunet/face_detection_yunet_2023mar.onnx"
)
DEFAULT_YUNET_MODEL = (
    Path(__file__).resolve().parent
    / "models"
    / "face_detection_yunet_2023mar.onnx"
)


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


class HaarFaceDetector:
    name = "haar"

    def __init__(self):
        self.classifier = cv2.CascadeClassifier(
            cv2.data.haarcascades
            + "haarcascade_frontalface_default.xml"
        )

        if self.classifier.empty():
            raise RuntimeError("Could not load Haar face detector.")

    def detect(self, frame):
        gray_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        faces = self.classifier.detectMultiScale(
            gray_frame,
            scaleFactor=1.1,
            minNeighbors=5,
            minSize=(60, 60),
        )
        return [tuple(map(int, face)) for face in faces]


class YuNetFaceDetector:
    name = "yunet"

    def __init__(self, model_path, score_threshold):
        if not hasattr(cv2, "FaceDetectorYN_create"):
            raise RuntimeError(
                "This OpenCV install does not include FaceDetectorYN/YuNet."
            )

        self.detector = cv2.FaceDetectorYN_create(
            str(model_path),
            "",
            (320, 320),
            score_threshold,
            0.3,
            5000,
        )
        self.input_size = None

    def detect(self, frame):
        frame_height, frame_width = frame.shape[:2]
        input_size = (frame_width, frame_height)

        if self.input_size != input_size:
            self.detector.setInputSize(input_size)
            self.input_size = input_size

        _, detections = self.detector.detect(frame)

        if detections is None:
            return []

        faces = []

        for detection in detections:
            x, y, width, height = detection[:4]
            faces.append(
                (
                    max(0, int(round(x))),
                    max(0, int(round(y))),
                    max(0, int(round(width))),
                    max(0, int(round(height))),
                )
            )

        return faces


def download_yunet_model(model_path):
    model_path.parent.mkdir(parents=True, exist_ok=True)
    print(f"Downloading YuNet model to {model_path}")
    urllib.request.urlretrieve(YUNET_MODEL_URL, model_path)


def create_face_detector(args):
    model_path = Path(args.yunet_model)

    if args.download_yunet and not model_path.exists():
        download_yunet_model(model_path)

    if args.detector in ("auto", "yunet") and model_path.exists():
        try:
            detector = YuNetFaceDetector(
                model_path=model_path,
                score_threshold=args.yunet_score,
            )
            print(f"Using YuNet face detector: {model_path}")
            return detector
        except RuntimeError as error:
            if args.detector == "yunet":
                raise
            print(f"YuNet unavailable, falling back to Haar: {error}")

    if args.detector == "yunet":
        raise RuntimeError(
            f"YuNet model not found: {model_path}. "
            "Run with --download-yunet or choose --detector haar."
        )

    print("Using Haar cascade face detector.")
    return HaarFaceDetector()


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
    parser.add_argument(
        "--detector",
        choices=("auto", "yunet", "haar"),
        default="auto",
        help="Face detector to use. Auto prefers YuNet if the model exists.",
    )
    parser.add_argument(
        "--yunet-model",
        default=str(DEFAULT_YUNET_MODEL),
        help="Path to OpenCV YuNet ONNX model.",
    )
    parser.add_argument(
        "--download-yunet",
        action="store_true",
        help="Download the OpenCV YuNet model if it is missing.",
    )
    parser.add_argument(
        "--yunet-score",
        type=float,
        default=0.75,
        help="YuNet confidence threshold. Lower detects more, higher is stricter.",
    )
    return parser.parse_args()


def main():
    args = parse_args()

    camera = cv2.VideoCapture(args.camera)

    if not camera.isOpened():
        print("Could not open the camera.")
        print("Close other apps using it or try another camera index.")
        return

    try:
        face_detector = create_face_detector(args)
    except RuntimeError as error:
        print(error)
        camera.release()
        return

    esp32 = ESP32Client(args.esp32_ip, args.esp32_port)

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

        faces = face_detector.detect(frame)

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

            command_sent = esp32.send_command(command)

            if command_sent:
                print(
                    f"Sent seq={sequence_number} "
                    f"pan={pan_command} tilt={tilt_command} "
                    f"error_x={command_error_x} error_y={command_error_y}"
                )

            sequence_number += 1
            last_command_time = current_time

        cv2.imshow(f"Face Detection ({face_detector.name})", frame)

        if cv2.waitKey(1) & 0xFF == ord("q"):
            break

    esp32.close()
    camera.release()
    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
