import cv2


def get_face_area(face):
    x, y, width, height = face
    return width * height


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

            error_text = f"Error X: {error_x}  Error Y: {error_y}"
            cv2.putText(
                frame,
                error_text,
                (10, 30),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.7,
                (0, 255, 0),
                2,
            )

        cv2.imshow("Face Detection", frame)

        if cv2.waitKey(1) & 0xFF == ord("q"):
            break

    camera.release()
    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
