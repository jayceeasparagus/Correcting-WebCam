import cv2


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

        for x, y, width, height in faces:
            top_left = (x, y)
            bottom_right = (x + width, y + height)
            cv2.rectangle(frame, top_left, bottom_right, (0, 255, 0), 2)

        cv2.imshow("Face Detection", frame)

        if cv2.waitKey(1) & 0xFF == ord("q"):
            break

    camera.release()
    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
