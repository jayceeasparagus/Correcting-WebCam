import cv2

DEADZONE_X = 40
DEADZONE_Y = 30
SMOOTHING_ALPHA = 0.3
MAX_MISSED_FRAMES = 5


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
        cv2.data.haarcascades + "haarcascade_frontalface_default.xml"
    )

    if not camera.isOpened():
        print("Could not open the camera.")
        print("Close other apps using the camera or try a different camera index.")
        return

    if face_cascade.empty():
        print("Could not load the face detector.")
        camera.release()
        return

    print("Camera opened. Face detection is running.")
    print("Press q in the video window to quit.")

    smoothed_error_x = None
    smoothed_error_y = None
    pan_command = "STOP"
    tilt_command = "STOP"
    missed_frames = 0

    while True:
        frame_read, frame = camera.read()

        if not frame_read:
            print("Could not read a frame from the camera.")
            break

        gray_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)

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

            top_left = (x, y)
            bottom_right = (x + width, y + height)
            cv2.rectangle(frame, top_left, bottom_right, (0, 255, 0), 2)

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
                    + (1 - SMOOTHING_ALPHA) * smoothed_error_x
                )
                smoothed_error_y = (
                    SMOOTHING_ALPHA * error_y
                    + (1 - SMOOTHING_ALPHA) * smoothed_error_y
                )

            smoothed_error_x = round(smoothed_error_x)
            smoothed_error_y = round(smoothed_error_y)
            missed_frames = 0

            pan_command = get_pan_command(smoothed_error_x)
            tilt_command = get_tilt_command(smoothed_error_y)

            error_text = (
                f"Smoothed X: {smoothed_error_x}  "
                f"Smoothed Y: {smoothed_error_y}"
            )
            command_text = f"Pan: {pan_command}  Tilt: {tilt_command}"

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
                    f"Face temporarily lost ({missed_frames}/{MAX_MISSED_FRAMES})  "
                    f"Pan: {pan_command}  Tilt: {tilt_command}"
                )
            else:
                pan_command = "STOP"
                tilt_command = "STOP"
                smoothed_error_x = None
                smoothed_error_y = None
                status_text = "No face - Pan: STOP  Tilt: STOP"

            cv2.putText(
                frame,
                status_text,
                (10, 30),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.6,
                (0, 255, 255),
                2,
            )

        cv2.imshow("Face Detection", frame)

        if cv2.waitKey(1) & 0xFF == ord("q"):
            break

    camera.release()
    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
